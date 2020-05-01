/*
 * Copyright 2011-2019 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */
#include <stdio.h>
#include <bx/bx.h>
#include <bx/spscqueue.h>
#include <bx/thread.h>
#include <bx/file.h>
#include <bx/timer.h>
#include <bx/math.h>

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
#include "camera.h"

#define MAX(a, b) ((a) > (b)) ? (a) : (b)

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


#include <bimg/decode.h>

void* load(bx::FileReaderI* _reader, bx::AllocatorI* _allocator, const char* _filePath, uint32_t* _size)
{
	if (bx::open(_reader, _filePath))
	{
		uint32_t size = (uint32_t)bx::getSize(_reader);
		void* data = BX_ALLOC(_allocator, size);
		bx::read(_reader, data, size);
		bx::close(_reader);
		if (NULL != _size)
		{
			*_size = size;
		}
		return data;
	}
	else
	{
		//DBG("Failed to open: %s.", _filePath);
	}

	if (NULL != _size)
	{
		*_size = 0;
	}

	return NULL;
}


void unload(void* _ptr)
{
	BX_FREE(getDefaultAllocator(), _ptr);
}

static void imageReleaseCb(void* _ptr, void* _userData)
{
	BX_UNUSED(_ptr);
	bimg::ImageContainer* imageContainer = (bimg::ImageContainer*)_userData;
	bimg::imageFree(imageContainer);
}



bgfx::TextureHandle loadTexture(bx::FileReaderI* _reader, const char* _filePath, uint64_t _flags, uint8_t _skip, bgfx::TextureInfo* _info, bimg::Orientation::Enum* _orientation)
{
	BX_UNUSED(_skip);
	bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

	uint32_t size;
	void* data = load(_reader,getDefaultAllocator(), _filePath, &size);
	if (NULL != data)
	{
		bimg::ImageContainer* imageContainer = bimg::imageParse(getDefaultAllocator(), data, size);

		if (NULL != imageContainer)
		{
			if (NULL != _orientation)
			{
				*_orientation = imageContainer->m_orientation;
			}

			const bgfx::Memory* mem = bgfx::makeRef(
				imageContainer->m_data
				, imageContainer->m_size
				, imageReleaseCb
				, imageContainer
			);
			unload(data);

			if (imageContainer->m_cubeMap)
			{
				handle = bgfx::createTextureCube(
					uint16_t(imageContainer->m_width)
					, 1 < imageContainer->m_numMips
					, imageContainer->m_numLayers
					, bgfx::TextureFormat::Enum(imageContainer->m_format)
					, _flags
					, mem
				);
			}
			else if (1 < imageContainer->m_depth)
			{
				handle = bgfx::createTexture3D(
					uint16_t(imageContainer->m_width)
					, uint16_t(imageContainer->m_height)
					, uint16_t(imageContainer->m_depth)
					, 1 < imageContainer->m_numMips
					, bgfx::TextureFormat::Enum(imageContainer->m_format)
					, _flags
					, mem
				);
			}
			else if (bgfx::isTextureValid(0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), _flags))
			{
				handle = bgfx::createTexture2D(
					uint16_t(imageContainer->m_width)
					, uint16_t(imageContainer->m_height)
					, 1 < imageContainer->m_numMips
					, imageContainer->m_numLayers
					, bgfx::TextureFormat::Enum(imageContainer->m_format)
					, _flags
					, mem
				);
			}

			if (bgfx::isValid(handle))
			{
				bgfx::setName(handle, _filePath);
			}

			if (NULL != _info)
			{
				bgfx::calcTextureSize(
					*_info
					, uint16_t(imageContainer->m_width)
					, uint16_t(imageContainer->m_height)
					, uint16_t(imageContainer->m_depth)
					, imageContainer->m_cubeMap
					, 1 < imageContainer->m_numMips
					, imageContainer->m_numLayers
					, bgfx::TextureFormat::Enum(imageContainer->m_format)
				);
			}
		}
	}

	return handle;
}

bgfx::TextureHandle loadTexture(const char* _name, uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, uint8_t _skip = 0, bgfx::TextureInfo* _info = NULL, bimg::Orientation::Enum* _orientation = NULL);

bgfx::TextureHandle loadTexture(const char* _name, uint64_t _flags, uint8_t _skip, bgfx::TextureInfo* _info, bimg::Orientation::Enum* _orientation)
{
	return loadTexture(s_fileReader, _name, _flags, _skip, _info, _orientation);
}
struct SampleData
{
	static constexpr uint32_t kNumSamples = 100;

	SampleData()
	{
		reset();
	}

	void reset()
	{
		m_offset = 0;
		bx::memSet(m_values, 0, sizeof(m_values));

		m_min = 0.0f;
		m_max = 0.0f;
		m_avg = 0.0f;
	}

	void pushSample(float value)
	{
		m_values[m_offset] = value;
		m_offset = (m_offset + 1) % kNumSamples;

		float min = bx::kFloatMax;
		float max = -bx::kFloatMax;
		float avg = 0.0f;

		for (uint32_t ii = 0; ii < kNumSamples; ++ii)
		{
			const float val = m_values[ii];
			min = bx::min(min, val);
			max = bx::max(max, val);
			avg += val;
		}

		m_min = min;
		m_max = max;
		m_avg = avg / kNumSamples;
	}

	int32_t m_offset;
	float m_values[kNumSamples];

	float m_min;
	float m_max;
	float m_avg;
};

static SampleData s_frameTime;

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

static int translateKey(int key)
{
	static int s_translateKey[256];
	static bool s_isInit = false;

	if (!s_isInit)
	{

		s_isInit = true;
		bx::memSet(s_translateKey, 0, sizeof(s_translateKey));
		s_translateKey[VK_ESCAPE] = Key::Esc;
		s_translateKey[VK_RETURN] = Key::Return;
		s_translateKey[VK_TAB] = Key::Tab;
		s_translateKey[VK_BACK] = Key::Backspace;
		s_translateKey[VK_SPACE] = Key::Space;
		s_translateKey[VK_UP] = Key::Up;
		s_translateKey[VK_DOWN] = Key::Down;
		s_translateKey[VK_LEFT] = Key::Left;
		s_translateKey[VK_RIGHT] = Key::Right;
		s_translateKey[VK_INSERT] = Key::Insert;
		s_translateKey[VK_DELETE] = Key::Delete;
		s_translateKey[VK_HOME] = Key::Home;
		s_translateKey[VK_END] = Key::End;
		s_translateKey[VK_PRIOR] = Key::PageUp;
		s_translateKey[VK_NEXT] = Key::PageDown;
		s_translateKey[VK_SNAPSHOT] = Key::Print;
		s_translateKey[VK_OEM_PLUS] = Key::Plus;
		s_translateKey[VK_OEM_MINUS] = Key::Minus;
		s_translateKey[VK_OEM_4] = Key::LeftBracket;
		s_translateKey[VK_OEM_6] = Key::RightBracket;
		s_translateKey[VK_OEM_1] = Key::Semicolon;
		s_translateKey[VK_OEM_7] = Key::Quote;
		s_translateKey[VK_OEM_COMMA] = Key::Comma;
		s_translateKey[VK_OEM_PERIOD] = Key::Period;
		s_translateKey[VK_DECIMAL] = Key::Period;
		s_translateKey[VK_OEM_2] = Key::Slash;
		s_translateKey[VK_OEM_5] = Key::Backslash;
		s_translateKey[VK_OEM_3] = Key::Tilde;
		s_translateKey[VK_F1] = Key::F1;
		s_translateKey[VK_F2] = Key::F2;
		s_translateKey[VK_F3] = Key::F3;
		s_translateKey[VK_F4] = Key::F4;
		s_translateKey[VK_F5] = Key::F5;
		s_translateKey[VK_F6] = Key::F6;
		s_translateKey[VK_F7] = Key::F7;
		s_translateKey[VK_F8] = Key::F8;
		s_translateKey[VK_F9] = Key::F9;
		s_translateKey[VK_F10] = Key::F10;
		s_translateKey[VK_F11] = Key::F11;
		s_translateKey[VK_F12] = Key::F12;
		s_translateKey[VK_NUMPAD0] = Key::NumPad0;
		s_translateKey[VK_NUMPAD1] = Key::NumPad1;
		s_translateKey[VK_NUMPAD2] = Key::NumPad2;
		s_translateKey[VK_NUMPAD3] = Key::NumPad3;
		s_translateKey[VK_NUMPAD4] = Key::NumPad4;
		s_translateKey[VK_NUMPAD5] = Key::NumPad5;
		s_translateKey[VK_NUMPAD6] = Key::NumPad6;
		s_translateKey[VK_NUMPAD7] = Key::NumPad7;
		s_translateKey[VK_NUMPAD8] = Key::NumPad8;
		s_translateKey[VK_NUMPAD9] = Key::NumPad9;
		s_translateKey[uint8_t('0')] = Key::Key0;
		s_translateKey[uint8_t('1')] = Key::Key1;
		s_translateKey[uint8_t('2')] = Key::Key2;
		s_translateKey[uint8_t('3')] = Key::Key3;
		s_translateKey[uint8_t('4')] = Key::Key4;
		s_translateKey[uint8_t('5')] = Key::Key5;
		s_translateKey[uint8_t('6')] = Key::Key6;
		s_translateKey[uint8_t('7')] = Key::Key7;
		s_translateKey[uint8_t('8')] = Key::Key8;
		s_translateKey[uint8_t('9')] = Key::Key9;
		s_translateKey[uint8_t('A')] = Key::KeyA;
		s_translateKey[uint8_t('B')] = Key::KeyB;
		s_translateKey[uint8_t('C')] = Key::KeyC;
		s_translateKey[uint8_t('D')] = Key::KeyD;
		s_translateKey[uint8_t('E')] = Key::KeyE;
		s_translateKey[uint8_t('F')] = Key::KeyF;
		s_translateKey[uint8_t('G')] = Key::KeyG;
		s_translateKey[uint8_t('H')] = Key::KeyH;
		s_translateKey[uint8_t('I')] = Key::KeyI;
		s_translateKey[uint8_t('J')] = Key::KeyJ;
		s_translateKey[uint8_t('K')] = Key::KeyK;
		s_translateKey[uint8_t('L')] = Key::KeyL;
		s_translateKey[uint8_t('M')] = Key::KeyM;
		s_translateKey[uint8_t('N')] = Key::KeyN;
		s_translateKey[uint8_t('O')] = Key::KeyO;
		s_translateKey[uint8_t('P')] = Key::KeyP;
		s_translateKey[uint8_t('Q')] = Key::KeyQ;
		s_translateKey[uint8_t('R')] = Key::KeyR;
		s_translateKey[uint8_t('S')] = Key::KeyS;
		s_translateKey[uint8_t('T')] = Key::KeyT;
		s_translateKey[uint8_t('U')] = Key::KeyU;
		s_translateKey[uint8_t('V')] = Key::KeyV;
		s_translateKey[uint8_t('W')] = Key::KeyW;
		s_translateKey[uint8_t('X')] = Key::KeyX;
		s_translateKey[uint8_t('Y')] = Key::KeyY;
		s_translateKey[uint8_t('Z')] = Key::KeyZ;

		
	}

	return s_translateKey[key];
}

static void glfw_keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	auto keyEvent = new KeyEvent;
	keyEvent->key = translateKey(key);
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

struct BrushData
{
	bool    m_raise;
	int32_t m_size;
	float   m_power;
	bx::Vec3   m_worldPosition;
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
			.add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true)
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



///////////////////////////////////////////////////////////////////////////////////////////////////

static const uint16_t s_terrainSize = 16;
static const uint32_t s_worldNumSectorsX = 32;
static const uint32_t s_worldNumSectorsY = 32;
static const uint32_t s_maxPatchesPerSector = 64;
static const uint32_t s_sectorSizeInMeters = 64;
static const uint32_t s_maxPatchesPerSectorRow = 8;
static const uint32_t s_maxPatchesPerSectorCol = 8;
static const uint32_t s_maxNodesInTree = 1 + 4 + 16 + 64 + 256 + 1024;

//////////////////////////////////////////////////////////////////////////////////////////////////

// terrain patch instance data
struct InstanceData
{
	float worldPosX;
	float worldPosY;
	float worldSize;
	float lodTransition;
	float heightMapAtlasU;
	float heightMapAtlasV;
	float pad0;
	float pad1;
};

struct QuadTreeNode
{
	float x;
	float z;
	int32_t	firstChildIndex;
	uint8_t lod;
};

struct TerrainData
{
	
	uint16_t*            m_heightMap;

	PosColorVertex*		 m_vertices;
	uint32_t             m_vertexCount;
	uint16_t*            m_indices;
	uint32_t             m_indexCount;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

static QuadTreeNode* s_quadTree;
static QuadTreeNode** s_nodesToRender;
static uint32_t s_numNodesToRender = 0;
static uint8_t* s_sectorsLODMap = NULL;
static uint32_t s_numPatches = 0;
static InstanceData* s_patches = NULL;

static uint32_t s_heightMapSize = 128;

//////////////////////////////////////////////////////////////////////////////////////////////////

void buildQuadTree(float* playerPosition)
{

	uint16_t nodeIndex = 0;
	QuadTreeNode* node = &s_quadTree[nodeIndex];
	// hack for height testing
	node->lod = 0;
	node->firstChildIndex = -1;
	node->x = 0;
	node->z = 0;
	++nodeIndex;

	uint16_t* nodesQueue = (uint16_t*)alloca(sizeof(uint16_t) * s_maxNodesInTree);
	uint16_t queueHead = 0;
	uint16_t queueSize = 0;
	// add head to queue
	queueSize++;
	nodesQueue[queueHead] = 0;

	while (queueSize > 0)
	{
		// pop from queue
		node = &s_quadTree[nodesQueue[queueHead]];
		++queueHead;
		--queueSize;

		float nodeSize = 64.0f * (1 << node->lod);
		float halfNodeSize = nodeSize * 0.5f;
		float nodeCenterX = node->x + halfNodeSize;
		float nodeCenterZ = node->z + halfNodeSize;
		float distance = (nodeCenterX - playerPosition[0]) * ((nodeCenterX - playerPosition[0])) +
			(nodeCenterZ - playerPosition[2]) * ((nodeCenterZ - playerPosition[2]));
		// check if distance is less than the sqrt(2) of the half (corner of the rect is the furtherest point from the center)
		if (node->lod > 0 && distance < halfNodeSize * halfNodeSize * 2.0f)
		{
			// split node to 4 child nodes
			// point parent node to first child
			node->firstChildIndex = nodeIndex;
			uint8_t nextLOD = node->lod - 1;
			QuadTreeNode* childNode;
			// first child
			childNode = &s_quadTree[nodeIndex];

			childNode->lod = nextLOD;
			childNode->firstChildIndex = -1;
			childNode->x = node->x;
			childNode->z = node->z;
			nodesQueue[queueHead + queueSize] = nodeIndex;
			++queueSize;
			++nodeIndex;
			// second child
			childNode = &s_quadTree[nodeIndex];

			childNode->lod = nextLOD;
			childNode->firstChildIndex = -1;
			childNode->x = node->x + halfNodeSize;
			childNode->z = node->z;
			nodesQueue[queueHead + queueSize] = nodeIndex;
			++queueSize;
			++nodeIndex;

			// third child
			childNode = &s_quadTree[nodeIndex];

			childNode->lod = nextLOD;
			childNode->firstChildIndex = -1;
			childNode->x = node->x;
			childNode->z = node->z + halfNodeSize;
			nodesQueue[queueHead + queueSize] = nodeIndex;
			++queueSize;
			++nodeIndex;

			// forth child
			childNode = &s_quadTree[nodeIndex];

			childNode->lod = nextLOD;
			childNode->firstChildIndex = -1;
			childNode->x = node->x + halfNodeSize;
			childNode->z = node->z + halfNodeSize;
			nodesQueue[queueHead + queueSize] = nodeIndex;
			++queueSize;
			++nodeIndex;
		}
	}

}

void generatePatchesFromNodes(QuadTreeNode** nodes, uint32_t numNodes)
{
	// generate patches from node
	uint8_t* sectorsLODMap = s_sectorsLODMap;
	InstanceData* instanceData = s_patches;
	for (uint32_t nodeIndex = 0; nodeIndex < numNodes; ++nodeIndex)
	{
		QuadTreeNode* node = nodes[nodeIndex];
		float patchSize = 8.0f * (1 << node->lod);
		//float nodeSize = patchSize * 8.0f;
		for (int j = 0; j < s_maxPatchesPerSectorCol; ++j)
		{
			for (int i = 0; i < s_maxPatchesPerSectorRow; ++i)
			{
				instanceData->worldSize = patchSize;
				instanceData->worldPosX = node->x + i * patchSize;
				instanceData->worldPosY = node->z + j * patchSize;
				instanceData->lodTransition = 0;
				instanceData->heightMapAtlasU = 0.125f * i;
				instanceData->heightMapAtlasV = 0.125f * j;




				uint32_t sectorX = ((uint32_t)instanceData->worldPosX) / 64;
				uint32_t sectorZ = ((uint32_t)instanceData->worldPosY) / 64;

				uint8_t mylod = sectorsLODMap[sectorX + sectorZ * s_worldNumSectorsX];
				uint8_t westlod = sectorX > 0 ? sectorsLODMap[sectorX - 1 + sectorZ * s_worldNumSectorsX] : 0;
				uint32_t nextSctorX = ((uint32_t)(instanceData->worldPosX + patchSize) / 64);
				uint8_t eastlod = nextSctorX < s_worldNumSectorsX ? sectorsLODMap[nextSctorX + sectorZ * s_worldNumSectorsX] : 0;
				uint8_t southlod = sectorZ > 0 ? sectorsLODMap[sectorX + (sectorZ - 1) * s_worldNumSectorsX] : 0;
				uint32_t nextSctorZ = ((uint32_t)(instanceData->worldPosY + patchSize) / 64);
				uint8_t northlod = nextSctorZ < s_worldNumSectorsY ? sectorsLODMap[sectorX + nextSctorZ * s_worldNumSectorsX] : 0;
				/*uint16_t packedLOD = 0 |
					MAX(westlod - mylod, 0) |
					MAX(eastlod - mylod, 0) << 4 |
					MAX(northlod - mylod, 0) << 8 |
					MAX(southlod - mylod, 0) << 12;*/
				uint16_t packedLOD = 0;
				uint32_t a = MAX(westlod - mylod, 0);
				uint32_t b = MAX(eastlod - mylod, 0);
				uint32_t c = MAX(northlod - mylod, 0);
				uint32_t d = MAX(southlod - mylod, 0);
				packedLOD = (uint16_t)(a | (b << 4) | (c << 8) | (d << 12));

				instanceData->lodTransition = (float)packedLOD;

				++instanceData;
				++s_numPatches;
			}
		}
	}
}

void traverseQuadTree(QuadTreeNode* node)
{
	if (node->firstChildIndex < 0)
	{
		uint8_t* sectorsLODMap = s_sectorsLODMap;
		// add note to sectors LOD map
		uint32_t secotrStartX = (uint32_t)node->x / 64;
		uint32_t secotrStartY = (uint32_t)node->z / 64;
		uint32_t numSectorsInNode = 1 << node->lod;
		for (uint32_t i = 0; i < numSectorsInNode; ++i)
		{
			memset(&sectorsLODMap[secotrStartX + (secotrStartY + i) * s_worldNumSectorsX], node->lod, numSectorsInNode);
		}
		s_nodesToRender[s_numNodesToRender++] = node;

	} // if a leaf node
	else
	{
		traverseQuadTree(&s_quadTree[node->firstChildIndex]);
		traverseQuadTree(&s_quadTree[node->firstChildIndex + 1]);
		traverseQuadTree(&s_quadTree[node->firstChildIndex + 2]);
		traverseQuadTree(&s_quadTree[node->firstChildIndex + 3]);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////

struct App
{
	void init(uint32_t windowWidth, uint32_t windowHeight);
	bool update();
	void handleKey(KeyEvent* keyEvent);

	MouseState s_mouseState;

private:
	void createTerrainMesh();

private:
	uint32_t m_windowWidth;
	uint32_t m_windowHeight;

	float	mousebuff[4];
	float	m_brushSize = 1.0;
	bool	m_renderGrid = false;

	TerrainData m_terrain;
	BrushData	m_brush;

	bgfx::TextureHandle m_gbufferTex[3];
	bgfx::UniformHandle s_albedo;
	bgfx::UniformHandle s_depth;
	bgfx::UniformHandle u_params;
	bgfx::UniformHandle u_viewProj;
	bgfx::UniformHandle u_invViewProj;
	bgfx::UniformHandle u_heightMapParams;
	bgfx::UniformHandle u_renderParams;

	bgfx::FrameBufferHandle m_gbuffer;

	bgfx::ProgramHandle m_program;
	bgfx::ProgramHandle m_combinedProgram;
	bgfx::ProgramHandle m_programComputeMousePos;
	bgfx::ProgramHandle m_programComputeUpdateHeightMap;

	bgfx::DynamicVertexBufferHandle m_mouseBufferHandle;
	bgfx::DynamicVertexBufferHandle m_mouseBufferHandle2;

	bgfx::VertexBufferHandle m_vbh;
	bgfx::IndexBufferHandle m_ibh;

	bgfx::VertexBufferHandle m_terrainVbh;
	bgfx::IndexBufferHandle m_terrainIbh;

	
	bgfx::ProgramHandle m_terrainHeightTextureProgram;
	bgfx::UniformHandle s_heightTexture;
	bgfx::TextureHandle m_heightTexture;

	bgfx::UniformHandle m_albedoTextureSampler;
	bgfx::TextureHandle m_albedoTexture;

	
};

static App theApp;

////////////////////////////////////////////////////

void App::init(uint32_t windowWidth, uint32_t windowHeight)
{
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
	m_vbh = bgfx::createVertexBuffer(
		// Static data can be passed with bgfx::makeRef
		bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices))
		, PosColorVertex::ms_layout
	);

	// Create static index buffer for triangle list rendering.
	m_ibh = bgfx::createIndexBuffer(
		// Static data can be passed with bgfx::makeRef
		bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList))
	);



	
	

	const uint64_t tsFlags = 0
		| BGFX_SAMPLER_MIN_POINT
		| BGFX_SAMPLER_MAG_POINT
		| BGFX_SAMPLER_MIP_POINT
		| BGFX_SAMPLER_U_CLAMP
		| BGFX_SAMPLER_V_CLAMP
		;

	bgfx::Attachment gbufferAt[3];
	

	uint32_t width = windowWidth;
	uint32_t height = windowHeight;

	m_windowWidth = width;
	m_windowHeight = height;

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



	// Set view 0 to the same dimensions as the window and to clear the color buffer.
	const bgfx::ViewId kClearView = 0;
	//bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR);
	bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);


	

	uint32_t heightMapWidth = 1024;
	uint32_t heightMapHeigt = 1024;
	uint32_t currFrame = UINT32_MAX;
	uint32_t readingFrame = 0;

	bgfx::TextureHandle heightTexture = bgfx::createTexture2D((uint16_t)heightMapWidth, (uint16_t)heightMapHeigt, false, 1, bgfx::TextureFormat::R16, 0 | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_MIN_POINT
		| BGFX_SAMPLER_MAG_POINT
		| BGFX_SAMPLER_MIP_POINT
		| BGFX_SAMPLER_U_CLAMP
		| BGFX_SAMPLER_V_CLAMP);

	bgfx::TextureHandle heightTextureCPU = bgfx::createTexture2D((uint16_t)heightMapWidth, (uint16_t)heightMapHeigt, false, 1, bgfx::TextureFormat::R16, 0 | BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_MIN_POINT
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
	//bgfx::ProgramHandle programCompute = bgfx::createProgram(loadShader("cs_update"), true);
	

	u_viewProj = bgfx::createUniform("u_myViewProj", bgfx::UniformType::Mat4);
	u_invViewProj = bgfx::createUniform("u_myInvViewProj", bgfx::UniformType::Mat4);
	s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
	s_depth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
	u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
	m_heightTexture.idx = bgfx::kInvalidHandle;
	s_heightTexture = bgfx::createUniform("s_heightTexture", bgfx::UniformType::Sampler);
	m_albedoTextureSampler = bgfx::createUniform("albedoTexture", bgfx::UniformType::Sampler);
	u_heightMapParams = bgfx::createUniform("u_heightMapParams", bgfx::UniformType::Vec4);
	u_renderParams = bgfx::createUniform("u_renderParams", bgfx::UniformType::Vec4);

	// Create program from shaders.
	m_program = loadProgram("vs_cubes", "fs_cubes");
	m_combinedProgram = loadProgram("vs_deferred_combine", "fs_deferred_combine");

	m_terrainHeightTextureProgram = loadProgram("vs_terrain_height_texture", "fs_terrain");
	m_albedoTexture = loadTexture("textures/forest_ground_01_dif.dds");

	m_programComputeMousePos = bgfx::createProgram(loadShader("cs_updateMousePos"), true);
	m_programComputeUpdateHeightMap = bgfx::createProgram(loadShader("cs_updateHeightMap"), true);

	uint32_t num = (s_terrainSize + 1) * (s_terrainSize + 1);
	m_terrain.m_vertices = (PosColorVertex*)BX_ALLOC(getDefaultAllocator(), num * sizeof(PosColorVertex));
	m_terrain.m_indices = (uint16_t*)BX_ALLOC(getDefaultAllocator(), num * sizeof(uint16_t) * 6);
	m_terrain.m_heightMap = (uint16_t*)BX_ALLOC(getDefaultAllocator(), sizeof(uint16_t) * s_heightMapSize * s_heightMapSize);

	bx::memSet(m_terrain.m_heightMap, 0, sizeof(uint16_t) * s_heightMapSize * s_heightMapSize);
	for (int i = 0; i < 64; ++i)
	{
		m_terrain.m_heightMap[i] = 5;
	}

	for (int i = 0; i < 17; ++i)
	{
		m_terrain.m_heightMap[i + s_heightMapSize * 17] = 25;
	}

	createTerrainMesh();

	

	m_mouseBufferHandle = bgfx::createDynamicVertexBuffer(1, Pos4Vertex::ms_layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
	

	s_sectorsLODMap = (uint8_t*)malloc(s_worldNumSectorsX * s_worldNumSectorsY);
	memset(s_sectorsLODMap, 0, s_worldNumSectorsX * s_worldNumSectorsY);
	s_quadTree = (QuadTreeNode*)malloc(sizeof(QuadTreeNode) * s_maxNodesInTree);
	s_nodesToRender = (QuadTreeNode**)malloc(sizeof(QuadTreeNode*) * s_maxNodesInTree);
	s_patches = (InstanceData*)malloc(sizeof(InstanceData) * s_maxNodesInTree * 64);

	cameraCreate();
	cameraSetPosition({ s_terrainSize / 2.0f, 40.0f, 0.0f });
	cameraSetVerticalAngle(-bx::kPiQuarter * 2);
	
}

void App::handleKey(KeyEvent* keyEvent)
{

	switch(keyEvent->key)
	{
	case Key::KeyW:
		{
			cameraSetKeyState(CAMERA_KEY_FORWARD, true);
			break;
		}
		case Key::KeyA:
		{
			cameraSetKeyState(CAMERA_KEY_LEFT, true);
			break;
		}
		case Key::KeyD:
		{
			cameraSetKeyState(CAMERA_KEY_RIGHT, true);
			break;
		}
		case Key::KeyS:
		{
			cameraSetKeyState(CAMERA_KEY_BACKWARD, true);
			break;
		}
		case Key::KeyE:
		{
			cameraSetKeyState(CAMERA_KEY_UP, true);
			break;
		}
		case Key::KeyC:
		{
			cameraSetKeyState(CAMERA_KEY_DOWN, true);
			break;
		}
	
	}
}

void App::createTerrainMesh()
{
	
	uint32_t colors[3];
	colors[0] = (uint32_t)(255 | 0 << 8 | 0 << 16 | 255 << 25);
	colors[1] = (uint32_t)(0 | 255 << 8 | 0 << 16 | 255 << 25);
	colors[2] = (uint32_t)(0 | 0 << 8 | 255 << 16 | 255 << 25);


	uint32_t startIndex = 0;
	m_terrain.m_vertexCount = 0;

	for (uint32_t y = 0; y <= s_terrainSize; y++)
	{

		startIndex = y % 2;
		uint32_t rowIndex = startIndex;


		for (uint32_t x = 0; x <= s_terrainSize; x++)
		{
			rowIndex = x % 2;
			rowIndex += startIndex;

			PosColorVertex* vert = &m_terrain.m_vertices[m_terrain.m_vertexCount];
			vert->m_x = (float)x / 16.0f;
			vert->m_y = 0.0;// m_terrain.m_heightMap[(y * s_terrainSize) + x];
			vert->m_z = (float)y / 16.0f;
			vert->m_abgr = colors[rowIndex];

			m_terrain.m_vertexCount++;
		}
	}

	int dir = 1;
	m_terrain.m_indexCount = 0;
	for (uint16_t y = 0; y < (s_terrainSize); y++)
	{
		uint16_t y_offset = (y * (s_terrainSize + 1));
		dir = 1 - (y % 2);
		for (uint16_t x = 0; x < (s_terrainSize); x++)
		{
			if (dir == 0)
			{
				m_terrain.m_indices[m_terrain.m_indexCount + 0] = y_offset + x + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 1] = y_offset + x + s_terrainSize + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 2] = y_offset + x;
				m_terrain.m_indices[m_terrain.m_indexCount + 3] = y_offset + x + s_terrainSize + 1 + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 4] = y_offset + x + s_terrainSize + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 5] = y_offset + x + 1;
			}
			else
			{
				m_terrain.m_indices[m_terrain.m_indexCount + 0] = y_offset + x + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 1] = y_offset + x + s_terrainSize + 1 + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 2] = y_offset + x;
				m_terrain.m_indices[m_terrain.m_indexCount + 3] = y_offset + x + s_terrainSize + 1 + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 4] = y_offset + x + s_terrainSize + 1;
				m_terrain.m_indices[m_terrain.m_indexCount + 5] = y_offset + x;
			}
			dir = 1 - dir;

			m_terrain.m_indexCount += 6;
		}
	}

	const bgfx::Memory* mem;
	mem = bgfx::makeRef(&m_terrain.m_vertices[0], sizeof(PosColorVertex) * m_terrain.m_vertexCount);
	m_terrainVbh = bgfx::createVertexBuffer(mem, PosColorVertex::ms_layout);

	mem = bgfx::makeRef(&m_terrain.m_indices[0], sizeof(uint16_t) * m_terrain.m_indexCount);
	m_terrainIbh = bgfx::createIndexBuffer(mem);


	if (!bgfx::isValid(m_heightTexture))
	{
		m_heightTexture = bgfx::createTexture2D((uint16_t)s_heightMapSize, (uint16_t)s_heightMapSize, false, 1, bgfx::TextureFormat::TextureFormat::R16, 0 | BGFX_TEXTURE_COMPUTE_WRITE |  BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
	}

	//mem = bgfx::makeRef(&m_terrain.m_heightMap[0], sizeof(uint16_t) * s_heightMapSize * s_heightMapSize);
	//bgfx::updateTexture2D(m_heightTexture, 0, 0, 0, 0, (uint16_t)s_heightMapSize, (uint16_t)s_heightMapSize, mem);
	
}

bool App::update()
{
	int64_t now = bx::getHPCounter();
	static int64_t last = now;
	const int64_t frameTime = now - last;
	last = now;
	const double freq = double(bx::getHPFrequency());
	const float deltaTime = float(frameTime / freq);

	uint32_t width = m_windowWidth;
	uint32_t height = m_windowHeight;
	const bgfx::Caps* caps = bgfx::getCaps();

	imguiBeginFrame(s_mouseState.m_mx
		, s_mouseState.m_my
		, (s_mouseState.m_buttons[0] ? IMGUI_MBUT_LEFT : 0)
		| (s_mouseState.m_buttons[1] ? IMGUI_MBUT_RIGHT : 0)
		| (s_mouseState.m_buttons[2] ? IMGUI_MBUT_MIDDLE : 0)
		, s_mouseState.m_mz, width, height);

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
	ImGui::Checkbox("Render grid", &m_renderGrid);
	ImGui::SliderFloat("Brush size", &m_brushSize, 1, 20);

	const bgfx::Stats* stats = bgfx::getStats();
	const double toMsCpu = 1000.0 / stats->cpuTimerFreq;
	const double toMsGpu = 1000.0 / stats->gpuTimerFreq;
	const double frameMs = double(stats->cpuTimeFrame)*toMsCpu;

	s_frameTime.pushSample(float(frameMs));

	char frameTextOverlay[256];
	bx::snprintf(frameTextOverlay, BX_COUNTOF(frameTextOverlay), "%s%.3fms, %s%.3fms\nAvg: %.3fms, %.1f FPS"
		, ICON_FA_ARROW_DOWN
		, s_frameTime.m_min
		, ICON_FA_ARROW_UP
		, s_frameTime.m_max
		, s_frameTime.m_avg
		, 1000.0f / s_frameTime.m_avg
	);

	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor(0.0f, 0.5f, 0.15f, 1.0f).Value);
	ImGui::PlotHistogram("Frame"
		, s_frameTime.m_values
		, SampleData::kNumSamples
		, s_frameTime.m_offset
		, frameTextOverlay
		, 0.0f
		, 60.0f
		, ImVec2(0.0f, 45.0f)
	);
	ImGui::PopStyleColor();

	ImGui::End();

	bool imguiMouseCapture = true;

	if (!ImGui::MouseOverArea())
	{
		imguiMouseCapture = false;
		// Update camera.
		cameraUpdate(deltaTime, s_mouseState);

		
	}

	imguiEndFrame();




	//if (readingFrame == currFrame)
	//{
	//	int ofer = 4;
	//}

	//bgfx::setImage(0, heightTexture, 0, bgfx::Access::ReadWrite);
	//bgfx::dispatch(0, programCompute, 1024 / 16, 1024 / 16);

	



	//if (currFrame == 100 && readingFrame == 0)
	//{
	//	bgfx::blit(0, heightTextureCPU, 0, 0, heightTexture, 0, 0);
	//	readingFrame = bgfx::readTexture(heightTextureCPU, heightmap2);
	//}


	//// This dummy draw call is here to make sure that view 0 is cleared if no other draw calls are submitted to view 0.
	bgfx::touch(0);
	// Set view 0 clear state.
	bgfx::setViewClear(0
		, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
		, 0x303030ff
		, 1.0f
		, 0
	);

	

	
	

	const bx::Vec3 at = { 0.0f, 0.0f,   0.0f };
	const bx::Vec3 eye = { 0.0f, 0.0f, -35.0f };

	float projView[16];
	float invProjView[16];

	// Set view and projection matrix for view 0.
	{
		float view[16];
		bx::mtxLookAt(view, eye, at);

		float proj[16];
		bx::mtxProj(proj, 60.0f, float(width) / float(height), 0.1f, 2000.0f, caps->homogeneousDepth);
		cameraGetViewMtx(view);
		bgfx::setViewTransform(0, view, proj);


		bx::mtxMul(projView, view, proj);
		bx::mtxInverse(invProjView, projView);

		// Set view 0 default viewport.
		bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
	}

	//mousebuff[0] = (float)s_mouseState.m_mx;
	//mousebuff[1] = (float)s_mouseState.m_my;
	//mousebuff[2] = (float)(s_mouseState.m_buttons[0] | s_mouseState.m_buttons[1] << 1);

	


	///////////////////////////////////////////////////////////

	static float ff = 0;
	s_numPatches = 0;
	memset(s_sectorsLODMap, 0, s_worldNumSectorsX * s_worldNumSectorsY);
	float playerPos[3];
	playerPos[0] = 100 + ff;
	playerPos[1] = 0;
	playerPos[2] = 100 + ff;

	buildQuadTree(playerPos);
	s_numNodesToRender = 0;
	traverseQuadTree(s_quadTree);
	generatePatchesFromNodes(s_nodesToRender, s_numNodesToRender);



	InstanceData* patches = s_patches;


	uint32_t numInstances = s_numPatches;
	uint16_t instanceStride = sizeof(InstanceData);
	if (numInstances == bgfx::getAvailInstanceDataBuffer(numInstances, instanceStride))
	{
		bgfx::InstanceDataBuffer idb;
		bgfx::allocInstanceDataBuffer(&idb, numInstances, instanceStride);

		uint8_t* data = idb.data;
		memcpy(data, patches, sizeof(InstanceData)* numInstances);


		float transform[16];
		bx::mtxSRT(transform, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		bgfx::setTransform(transform);

		// Set instance data buffer.
		bgfx::setInstanceDataBuffer(&idb);
		bgfx::setVertexBuffer(0, m_terrainVbh);
		bgfx::setIndexBuffer(m_terrainIbh);
		bgfx::setTexture(0, s_heightTexture, m_heightTexture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
		bgfx::setTexture(1, m_albedoTextureSampler, m_albedoTexture,0);
		//bgfx::setState(BGFX_STATE_DEFAULT| BGFX_STATE_PT_LINES);
		float val[4];
		val[0] = 0.1f; // height map scale
		val[1] = 0.0f; // sea level
		val[2] = 0.125f; // size of patch inside height texture
		bgfx::setUniform(u_heightMapParams, val);
		val[0] = (float)m_renderGrid;
		val[1] = m_brush.m_worldPosition.x;
		val[2] = m_brush.m_worldPosition.y;
		val[3] = m_brush.m_worldPosition.z;

		bgfx::setUniform(u_renderParams, val);

		bgfx::submit(0, m_terrainHeightTextureProgram);

	}

	///////////////////////////////////////////////////////////

	float params[4];
	params[0] = (float)s_mouseState.m_mx / (float)width;
	params[1] = (float)s_mouseState.m_my / (float)height;
	params[2] = (float)(s_mouseState.m_buttons[0] | s_mouseState.m_buttons[1] << 1);
	params[3] = m_brushSize;
	bgfx::setUniform(u_params, params, 1);
	bgfx::setUniform(u_invViewProj, invProjView, 1);
	
	//bgfx::setImage(0, m_gbufferTex[2], 0, bgfx::Access::Read);
	bgfx::setTexture(0, s_depth, m_gbufferTex[2]);
	bgfx::setBuffer(1, m_mouseBufferHandle, bgfx::Access::Write);
	bgfx::dispatch(1, m_programComputeMousePos, 1, 1);

	if (!imguiMouseCapture && s_mouseState.m_buttons[0])
	{
		bgfx::setImage(0, m_heightTexture, 0, bgfx::Access::ReadWrite, bgfx::TextureFormat::R16);
		bgfx::setBuffer(1, m_mouseBufferHandle, bgfx::Access::Read);
		bgfx::dispatch(2, m_programComputeUpdateHeightMap, s_heightMapSize / 8, s_heightMapSize /8);
		//bgfx::dispatch(2, m_programComputeUpdateHeightMap, 1, 1);
		/*static float buff[129 * 129];
		static float f = 0;
		const bgfx::Memory* mem = bgfx::makeRef(buff, sizeof(buff));
		f += 0.01f;
		buff[0] = f;*/
		//bgfx::updateTexture2D(m_heightTexture, 0, 0, 0, 0, (uint16_t)s_heightMapSize, (uint16_t)s_heightMapSize, mem);
	}
	

	// screen space quad

	float proj[16];
	bx::mtxOrtho(proj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, caps->homogeneousDepth);
	bgfx::setViewTransform(2, NULL, proj);

	bgfx::setState(0
		| BGFX_STATE_WRITE_RGB
		//| BGFX_STATE_WRITE_A
	);
	

	bgfx::setViewRect(2, 0, 0, uint16_t(width), uint16_t(height));
	bgfx::setTexture(0, s_albedo, m_gbufferTex[0]);
	bgfx::setTexture(1, s_depth, m_gbufferTex[2]);
	screenSpaceQuad((float)width, (float)height, 0, caps->originBottomLeft);
	bgfx::setBuffer(2, m_mouseBufferHandle, bgfx::Access::Read);
	bgfx::submit(2, m_combinedProgram);

	uint32_t currFrame = bgfx::frame();

	return true;
}

////////////////////////////////////////////////////


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

	theApp.init(args->width, args->height);

	bool exit = false;

	
	while (!exit) {
		// Handle events from the main thread.
		while (auto ev = (EventType *)s_apiThreadEvents.pop()) {
			if (*ev == EventType::Key) {
				auto keyEvent = (KeyEvent *)ev;
				/*if (keyEvent->key == GLFW_KEY_F1 && keyEvent->action == GLFW_RELEASE)
					showStats = !showStats;*/
				theApp.handleKey(keyEvent);
			}
			else if (*ev == EventType::MouseCursor) {
				auto mouseCursorEvent = (MouseCursorEvent *)ev;
				theApp.s_mouseState.m_mx = (int32_t)mouseCursorEvent->x;
				theApp.s_mouseState.m_my = (int32_t)mouseCursorEvent->y;
			}
			else if (*ev == EventType::MouseButton) {
				auto mouseButtonEvent = (MouseButtonEvent *)ev;
				theApp.s_mouseState.m_buttons[mouseButtonEvent->button] = !!mouseButtonEvent->action;
			}
			else if (*ev == EventType::Resize) {
				auto resizeEvent = (ResizeEvent *)ev;
				bgfx::reset(resizeEvent->width, resizeEvent->height, BGFX_RESET_VSYNC);
				/*bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
				width = resizeEvent->width;
				height = resizeEvent->height;*/
			} else if (*ev == EventType::Exit) {
				exit = true;
			}
			delete ev;
		}

		theApp.update();

		
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
