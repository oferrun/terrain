/*
 * Copyright 2011-2019 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */
#include <stdio.h>
#include <bx/bx.h>
#include <bx/spscqueue.h>
#include <bx/thread.h>
#include <bx/file.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>


#include "../bgfx/examples/common/imgui/imgui.h"

/////////////////////////////////////
//// utils

bx::AllocatorI* getDefaultAllocator()
{
	BX_PRAGMA_DIAGNOSTIC_PUSH();
	BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4459); // warning C4459: declaration of 's_allocator' hides global declaration
	BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wshadow");
	static bx::DefaultAllocator s_allocator;
	return &s_allocator;
	BX_PRAGMA_DIAGNOSTIC_POP();
}
bx::AllocatorI* g_allocator = getDefaultAllocator();

typedef bx::StringT<&g_allocator> String;

static String s_currentDir;

void setCurrentDir(const char* _dir)
{
	s_currentDir.set(_dir);
}

class FileReader : public bx::FileReader
{
	typedef bx::FileReader super;

public:
	virtual bool open(const bx::FilePath& _filePath, bx::Error* _err) override
	{
		String filePath(s_currentDir);
		filePath.append(_filePath);
		return super::open(filePath.getPtr(), _err);
	}
};

static bx::FileReaderI* s_fileReader = NULL;

static const bgfx::Memory* loadMem(bx::FileReaderI* _reader, const char* _filePath)
{
	if (bx::open(_reader, _filePath))
	{
		uint32_t size = (uint32_t)bx::getSize(_reader);
		const bgfx::Memory* mem = bgfx::alloc(size + 1);
		bx::read(_reader, mem->data, size);
		bx::close(_reader);
		mem->data[mem->size - 1] = '\0';
		return mem;
	}

	//DBG("Failed to load %s.", _filePath);
	return NULL;
}

static bgfx::ShaderHandle loadShader(bx::FileReaderI* _reader, const char* _name)
{
	char filePath[512];

	const char* shaderPath = "???";

	switch (bgfx::getRendererType())
	{
	case bgfx::RendererType::Noop:
	case bgfx::RendererType::Direct3D9:  shaderPath = "shaders/dx9/";   break;
	case bgfx::RendererType::Direct3D11:
	case bgfx::RendererType::Direct3D12: shaderPath = "shaders/dx11/";  break;
	case bgfx::RendererType::Gnm:        shaderPath = "shaders/pssl/";  break;
	case bgfx::RendererType::Metal:      shaderPath = "shaders/metal/"; break;
	case bgfx::RendererType::Nvn:        shaderPath = "shaders/nvn/";   break;
	case bgfx::RendererType::OpenGL:     shaderPath = "shaders/glsl/";  break;
	case bgfx::RendererType::OpenGLES:   shaderPath = "shaders/essl/";  break;
	case bgfx::RendererType::Vulkan:     shaderPath = "shaders/spirv/"; break;

	case bgfx::RendererType::Count:
		BX_CHECK(false, "You should not be here!");
		break;
	}

	bx::strCopy(filePath, BX_COUNTOF(filePath), shaderPath);
	bx::strCat(filePath, BX_COUNTOF(filePath), _name);
	bx::strCat(filePath, BX_COUNTOF(filePath), ".bin");

	bgfx::ShaderHandle handle = bgfx::createShader(loadMem(_reader, filePath));
	bgfx::setName(handle, _name);

	return handle;
}

bgfx::ShaderHandle loadShader(const char* _name)
{
	return loadShader(s_fileReader, _name);
}

bgfx::ProgramHandle loadProgram(bx::FileReaderI* _reader, const char* _vsName, const char* _fsName)
{
	bgfx::ShaderHandle vsh = loadShader(_reader, _vsName);
	bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
	if (NULL != _fsName)
	{
		fsh = loadShader(_reader, _fsName);
	}

	return bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);
}

bgfx::ProgramHandle loadProgram(const char* _vsName, const char* _fsName)
{
	return loadProgram(s_fileReader, _vsName, _fsName);
}

/////////////////////////////////////

static bx::DefaultAllocator s_allocator;
static bx::SpScUnboundedQueue s_apiThreadEvents(&s_allocator);

enum class EventType
{
	Exit,
	Key,
	Resize,
	MouseButton,
	MouseCursor
};

struct ExitEvent
{
	EventType type = EventType::Exit;
};

struct KeyEvent
{
	EventType type = EventType::Key;
	int key;
	int action;
};

struct ResizeEvent
{
	EventType type = EventType::Resize;
	uint32_t width;
	uint32_t height;
};

struct MouseButtonEvent
{
	EventType type = EventType::MouseButton;
	uint32_t button;
	uint32_t action;
	uint32_t modifiers;
};

struct MouseCursorEvent
{
	EventType type = EventType::MouseCursor;
	double x;
	double y;
};

static void glfw_errorCallback(int error, const char *description)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static void glfw_keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	auto keyEvent = new KeyEvent;
	keyEvent->key = key;
	keyEvent->action = action;
	s_apiThreadEvents.push(keyEvent);
}

static void glfw_mouseButtonsCallback(GLFWwindow* window, int button, int action, int modifiers)
{
	auto mouseButtonEvent = new MouseButtonEvent;
	mouseButtonEvent->button = button;
	mouseButtonEvent->action = action;
	mouseButtonEvent->modifiers = modifiers;
	s_apiThreadEvents.push(mouseButtonEvent);
}

static void glfw_mouseCursorCallback(GLFWwindow* window, double x, double y )
{
	auto mouseCursorEvent = new MouseCursorEvent;
	mouseCursorEvent->x = x;
	mouseCursorEvent->y = y;	
	s_apiThreadEvents.push(mouseCursorEvent);
}

struct ApiThreadArgs
{
	bgfx::PlatformData platformData;
	uint32_t width;
	uint32_t height;
};


struct PosColorVertex
{
	float m_x;
	float m_y;
	float m_z;
	uint32_t m_abgr;

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
	};

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosColorVertex::ms_layout;

struct Pos4Vertex
{
	float m_x;
	float m_y;
	float m_z;
	float m_w;

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float)
			.end();
	};

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout Pos4Vertex::ms_layout;

static PosColorVertex s_cubeVertices[] =
{
	{-1.0f,  1.0f,  1.0f, 0xff000000 },
	{ 1.0f,  1.0f,  1.0f, 0xff0000ff },
	{-1.0f, -1.0f,  1.0f, 0xff00ff00 },
	{ 1.0f, -1.0f,  1.0f, 0xff00ffff },
	{-1.0f,  1.0f, -1.0f, 0xffff0000 },
	{ 1.0f,  1.0f, -1.0f, 0xffff00ff },
	{-1.0f, -1.0f, -1.0f, 0xffffff00 },
	{ 1.0f, -1.0f, -1.0f, 0xffffffff },
};

static const uint16_t s_cubeTriList[] =
{
	0, 1, 2, // 0
	1, 3, 2,
	4, 6, 5, // 2
	5, 6, 7,
	0, 2, 4, // 4
	4, 2, 6,
	1, 5, 3, // 6
	5, 7, 3,
	0, 4, 1, // 8
	4, 5, 1,
	2, 3, 6, // 10
	6, 3, 7,
};

struct PosTexCoord0Vertex
{
	float m_x;
	float m_y;
	float m_z;
	float m_u;
	float m_v;

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	}

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosTexCoord0Vertex::ms_layout;

void screenSpaceQuad(float _textureWidth, float _textureHeight, float _texelHalf, bool _originBottomLeft, float _width = 1.0f, float _height = 1.0f)
{
	if (3 == bgfx::getAvailTransientVertexBuffer(3, PosTexCoord0Vertex::ms_layout))
	{
		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 3, PosTexCoord0Vertex::ms_layout);
		PosTexCoord0Vertex* vertex = (PosTexCoord0Vertex*)vb.data;

		const float minx = -_width;
		const float maxx = _width;
		const float miny = 0.0f;
		const float maxy = _height * 2.0f;

		const float texelHalfW = _texelHalf / _textureWidth;
		const float texelHalfH = _texelHalf / _textureHeight;
		const float minu = -1.0f + texelHalfW;
		const float maxu = 1.0f + texelHalfH;

		const float zz = 0.0f;

		float minv = texelHalfH;
		float maxv = 2.0f + texelHalfH;

		if (_originBottomLeft)
		{
			float temp = minv;
			minv = maxv;
			maxv = temp;

			minv -= 1.0f;
			maxv -= 1.0f;
		}

		vertex[0].m_x = minx;
		vertex[0].m_y = miny;
		vertex[0].m_z = zz;
		vertex[0].m_u = minu;
		vertex[0].m_v = minv;

		vertex[1].m_x = maxx;
		vertex[1].m_y = miny;
		vertex[1].m_z = zz;
		vertex[1].m_u = maxu;
		vertex[1].m_v = minv;

		vertex[2].m_x = maxx;
		vertex[2].m_y = maxy;
		vertex[2].m_z = zz;
		vertex[2].m_u = maxu;
		vertex[2].m_v = maxv;

		bgfx::setVertexBuffer(0, &vb);
	}
}



static int32_t runApiThread(bx::Thread *self, void *userData)
{
	auto args = (ApiThreadArgs *)userData;
	// Initialize bgfx using the native window handle and window resolution.
	bgfx::Init init;
	init.platformData = args->platformData;
	init.resolution.width = args->width;
	init.resolution.height = args->height;
	init.resolution.reset = BGFX_RESET_NONE;
	if (!bgfx::init(init))
		return 1;

	// Get renderer capabilities info.
	const bgfx::Caps* caps = bgfx::getCaps();

	setCurrentDir("");
	s_fileReader = BX_NEW(g_allocator, FileReader);

	// Imgui.
	imguiCreate();

	// Create vertex stream declaration.
	PosColorVertex::init();
	PosTexCoord0Vertex::init();
	Pos4Vertex::init();

	// Create static vertex buffer.
	bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(
		// Static data can be passed with bgfx::makeRef
		bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices))
		, PosColorVertex::ms_layout
	);

	// Create static index buffer for triangle list rendering.
	bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(
		// Static data can be passed with bgfx::makeRef
		bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList))
	);

	// Create program from shaders.
	bgfx::ProgramHandle program = loadProgram("vs_cubes", "fs_cubes");
	bgfx::ProgramHandle combinedProgram = loadProgram("vs_deferred_combine", "fs_deferred_combine");


	const uint64_t tsFlags = 0
		| BGFX_SAMPLER_MIN_POINT
		| BGFX_SAMPLER_MAG_POINT
		| BGFX_SAMPLER_MIP_POINT
		| BGFX_SAMPLER_U_CLAMP
		| BGFX_SAMPLER_V_CLAMP
		;

	bgfx::Attachment gbufferAt[3];
	bgfx::TextureHandle m_gbufferTex[3];
	
	bgfx::FrameBufferHandle m_gbuffer;

	uint32_t width = args->width;
	uint32_t height = args->height;

	/*m_gbufferTex[0] = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::BGRA8, 0 | tsFlags);
	const bgfx::Memory* mem = bgfx::alloc(width * height * 4);
	memset(mem->data, 0xFF, width * height * 4);
	bgfx::updateTexture2D(m_gbufferTex[0], 0, 0, 0, 0, uint16_t(width), uint16_t(height), mem);*/

	{
		m_gbufferTex[0] = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | tsFlags);
		m_gbufferTex[1] = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | tsFlags);
		gbufferAt[0].init(m_gbufferTex[0]);
		gbufferAt[1].init(m_gbufferTex[1]);
	}

	m_gbufferTex[2] = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT | tsFlags);
	gbufferAt[2].init(m_gbufferTex[2]);

	m_gbuffer = bgfx::createFrameBuffer(BX_COUNTOF(gbufferAt), gbufferAt, true);

	bgfx::setViewFrameBuffer(0, m_gbuffer);

	bgfx::UniformHandle s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
	bgfx::UniformHandle s_depth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
	bgfx::UniformHandle u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);

	// Set view 0 to the same dimensions as the window and to clear the color buffer.
	const bgfx::ViewId kClearView = 0;
	//bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR);
	bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
	
	bool showStats = false;
	bool exit = false;
	struct MouseState
	{
		MouseState()
			: m_mx(0)
			, m_my(0)
			, m_mz(0)
		{
			for (uint32_t ii = 0; ii < 4; ++ii)
			{
				m_buttons[ii] = 0;
			}
		}

		int32_t m_mx;
		int32_t m_my;
		int32_t m_mz;
		uint8_t m_buttons[4];
	};

	uint32_t heightMapWidth = 1024;
	uint32_t heightMapHeigt = 1024;
	uint32_t currFrame = UINT32_MAX;
	uint32_t readingFrame = 0;

	bgfx::TextureHandle heightTexture = bgfx::createTexture2D((uint16_t)heightMapWidth, (uint16_t)heightMapHeigt, false, 1, bgfx::TextureFormat::R16, 0 | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_MIN_POINT
		| BGFX_SAMPLER_MAG_POINT
		| BGFX_SAMPLER_MIP_POINT
		| BGFX_SAMPLER_U_CLAMP
		| BGFX_SAMPLER_V_CLAMP);

	bgfx::TextureHandle heightTextureCPU = bgfx::createTexture2D((uint16_t)heightMapWidth, (uint16_t)heightMapHeigt, false, 1, bgfx::TextureFormat::R16, 0 | BGFX_TEXTURE_READ_BACK| BGFX_TEXTURE_BLIT_DST|BGFX_SAMPLER_MIN_POINT
		| BGFX_SAMPLER_MAG_POINT
		| BGFX_SAMPLER_MIP_POINT
		| BGFX_SAMPLER_U_CLAMP
		| BGFX_SAMPLER_V_CLAMP);
	const bgfx::Memory* mem;
	uint16_t* heightmap = (uint16_t*)malloc(heightMapWidth * heightMapHeigt * sizeof(uint16_t));
	uint16_t* heightmap2 = (uint16_t*)malloc(heightMapWidth * heightMapHeigt * sizeof(uint16_t));
	memset(heightmap, 0xEE, heightMapWidth * heightMapHeigt * sizeof(uint16_t));
	mem = bgfx::makeRef(&heightmap[0], sizeof(uint16_t) * heightMapWidth * heightMapHeigt);
	bgfx::updateTexture2D(heightTexture, 0, 0, 0, 0, (uint16_t)heightMapWidth, (uint16_t)heightMapHeigt, mem);
	memset(heightmap2, 0xAA, heightMapWidth * heightMapHeigt * sizeof(uint16_t));
	bgfx::UniformHandle s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
	bgfx::ProgramHandle programCompute = bgfx::createProgram(loadShader("cs_update"), true);

	bgfx::UniformHandle u_viewProj = bgfx::createUniform("u_myViewProj", bgfx::UniformType::Mat4);
	bgfx::UniformHandle u_invViewProj = bgfx::createUniform("u_myInvViewProj", bgfx::UniformType::Mat4);

	float mousebuff[4];
	mousebuff[0] = 512.0f;
	mousebuff[1] = 384.0f;
	mousebuff[2] = 0.0f;
	mousebuff[3] = 0.0f;
	const bgfx::Memory* memmouse = bgfx::makeRef(mousebuff, sizeof(mousebuff));
	bgfx::DynamicVertexBufferHandle mouseBufferHandle = bgfx::createDynamicVertexBuffer(memmouse, Pos4Vertex::ms_layout, BGFX_BUFFER_COMPUTE_READ/*BGFX_BUFFER_COMPUTE_READ_WRITE*/);

	float brushSize = 1.0;

	static MouseState mouseState;
	while (!exit) {
		// Handle events from the main thread.
		while (auto ev = (EventType *)s_apiThreadEvents.pop()) {
			if (*ev == EventType::Key) {
				auto keyEvent = (KeyEvent *)ev;
				if (keyEvent->key == GLFW_KEY_F1 && keyEvent->action == GLFW_RELEASE)
					showStats = !showStats;
			}
			else if (*ev == EventType::MouseCursor) {
				auto mouseCursorEvent = (MouseCursorEvent *)ev;
				mouseState.m_mx = (int32_t)mouseCursorEvent->x;
				mouseState.m_my = (int32_t)mouseCursorEvent->y;
			}
			else if (*ev == EventType::MouseButton) {
				auto mouseButtonEvent = (MouseButtonEvent *)ev;
				mouseState.m_buttons[mouseButtonEvent->button] = !!mouseButtonEvent->action;
			}
			else if (*ev == EventType::Resize) {
				auto resizeEvent = (ResizeEvent *)ev;
				bgfx::reset(resizeEvent->width, resizeEvent->height, BGFX_RESET_VSYNC);
				bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
				width = resizeEvent->width;
				height = resizeEvent->height;
			} else if (*ev == EventType::Exit) {
				exit = true;
			}
			delete ev;
		}

		imguiBeginFrame(mouseState.m_mx
			, mouseState.m_my
			, (mouseState.m_buttons[0] ? IMGUI_MBUT_LEFT : 0)
			| (mouseState.m_buttons[1] ? IMGUI_MBUT_RIGHT : 0)
			| (mouseState.m_buttons[2] ? IMGUI_MBUT_MIDDLE : 0)
			, mouseState.m_mz, width, height);

		ImGui::SetNextWindowPos(
			ImVec2(width - width / 5.0f - 10.0f, 10.0f)
			, ImGuiCond_FirstUseEver
		);
		ImGui::SetNextWindowSize(
			ImVec2(width / 5.0f, height / 3.0f)
			, ImGuiCond_FirstUseEver
		);
		ImGui::Begin("Settings"
			, NULL
			, 0
		);
		ImGui::SliderFloat("Brush size", &brushSize, 1, 20);

		ImGui::End();

		imguiEndFrame();




		if (readingFrame == currFrame)
		{
			int ofer = 4;
		}

		bgfx::setImage(0, heightTexture, 0, bgfx::Access::ReadWrite);
		bgfx::dispatch(0, programCompute, 1024 / 16, 1024 / 16);

		

		if (currFrame == 100 && readingFrame == 0)
		{
			bgfx::blit(0, heightTextureCPU, 0, 0, heightTexture, 0, 0);
			readingFrame = bgfx::readTexture(heightTextureCPU, heightmap2);
		}
		

		//// This dummy draw call is here to make sure that view 0 is cleared if no other draw calls are submitted to view 0.
		bgfx::touch(kClearView);
		// Set view 0 clear state.
		bgfx::setViewClear(0
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x303030ff
			, 1.0f
			, 0
		);

		mousebuff[0] = (float)mouseState.m_mx;
		mousebuff[1] = (float)mouseState.m_my;
		mousebuff[2] = (float)(mouseState.m_buttons[0] | mouseState.m_buttons[1] << 1);
		const bgfx::Memory* memmouse2 = bgfx::makeRef(mousebuff, sizeof(mousebuff));
		bgfx::update(mouseBufferHandle, 0, memmouse2);

		const bx::Vec3 at = { 0.0f, 0.0f,   0.0f };
		const bx::Vec3 eye = { 0.0f, 0.0f, -35.0f };

		float projView[16];
		float invProjView[16];

		// Set view and projection matrix for view 0.
		{
			float view[16];
			bx::mtxLookAt(view, eye, at);

			float proj[16];
			bx::mtxProj(proj, 60.0f, float(width) / float(height), 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
			bgfx::setViewTransform(0, view, proj);

			
			bx::mtxMul(projView, view, proj);
			bx::mtxInverse(invProjView, projView);

			float params[4];
			params[0] = 1.0f / (float)width;
			params[1] = 1.0f / (float)height;
			params[2] = brushSize;
			bgfx::setUniform(u_params, params, 1);

			// Set view 0 default viewport.
			bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
		}

		float mtx[16];
		//bx::mtxRotateXY(mtx, 0, 0);
		bx::mtxScale(mtx, 10);
		mtx[12] = 0;
		mtx[13] = 0;
		mtx[14] = 0.0f;

		uint64_t state = 0
			| BGFX_STATE_WRITE_R
			| BGFX_STATE_WRITE_G
			| BGFX_STATE_WRITE_B
			| BGFX_STATE_WRITE_A
			| BGFX_STATE_WRITE_Z
			| BGFX_STATE_DEPTH_TEST_LESS
			| BGFX_STATE_CULL_CW
			| BGFX_STATE_MSAA;

		// Set model matrix for rendering.
		bgfx::setTransform(mtx);

		// Set vertex and index buffer.
		bgfx::setVertexBuffer(0, vbh);
		bgfx::setIndexBuffer(ibh);

		// Set render states.
		bgfx::setState(state);

		// Submit primitive for rendering to view 0.
		bgfx::submit(0, program);

		

		float proj[16];
		bx::mtxOrtho(proj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, caps->homogeneousDepth);
		bgfx::setViewTransform(1, NULL, proj);

		bgfx::setState(0
			| BGFX_STATE_WRITE_RGB
			//| BGFX_STATE_WRITE_A
		);
		bgfx::setUniform(u_invViewProj, invProjView, 1);
		
		bgfx::setViewRect(1, 0, 0, uint16_t(width), uint16_t(height));
		bgfx::setTexture(0, s_albedo, m_gbufferTex[0]);
		bgfx::setTexture(1, s_depth, m_gbufferTex[2]);
		screenSpaceQuad((float)width, (float)height, 0, caps->originBottomLeft);
		bgfx::setBuffer(2, mouseBufferHandle, bgfx::Access::Read);
		bgfx::submit(1, combinedProgram);

		
		currFrame = bgfx::frame();
		int ofer = 4;
	}

	imguiDestroy();
	
	bgfx::shutdown();
	return 0;
}

int main(int argc, char **argv)
{
	// Create a GLFW window without an OpenGL context.
	glfwSetErrorCallback(glfw_errorCallback);
	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window = glfwCreateWindow(1280, 800, "helloworld multithreaded", nullptr, nullptr);
	if (!window)
		return 1;
	glfwSetKeyCallback(window, glfw_keyCallback);
	glfwSetMouseButtonCallback(window, glfw_mouseButtonsCallback);
	glfwSetCursorPosCallback(window, glfw_mouseCursorCallback);
	// Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
	// Most graphics APIs must be used on the same thread that created the window.
	bgfx::renderFrame();
	// Create a thread to call the bgfx API from (except bgfx::renderFrame).
	ApiThreadArgs apiThreadArgs;
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
	apiThreadArgs.platformData.ndt = glfwGetX11Display();
	apiThreadArgs.platformData.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
	apiThreadArgs.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
	apiThreadArgs.platformData.nwh = glfwGetWin32Window(window);
#endif
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	apiThreadArgs.width = (uint32_t)width;
	apiThreadArgs.height = (uint32_t)height;
	bx::Thread apiThread;
	apiThread.init(runApiThread, &apiThreadArgs);
	// Run GLFW message pump.
	bool exit = false;
	while (!exit) {
		glfwPollEvents();
		// Send window close event to the API thread.
		if (glfwWindowShouldClose(window)) {
			s_apiThreadEvents.push(new ExitEvent);
			exit = true;
		}
		// Send window resize event to the API thread.
		int oldWidth = width, oldHeight = height;
		glfwGetWindowSize(window, &width, &height);
		if (width != oldWidth || height != oldHeight) {
			auto resize = new ResizeEvent;
			resize->width = (uint32_t)width;
			resize->height = (uint32_t)height;
			s_apiThreadEvents.push(resize);
		}
		// Wait for the API thread to call bgfx::frame, then process submitted rendering primitives.
		bgfx::renderFrame();
	}
	// Wait for the API thread to finish before shutting down.
	while (bgfx::RenderFrame::NoContext != bgfx::renderFrame()) {}
	apiThread.shutdown();
	glfwTerminate();
	return apiThread.getExitCode();
}
