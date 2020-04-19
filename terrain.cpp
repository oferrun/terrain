/*
 * Copyright 2011-2019 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */
#include <stdio.h>
#include <bx/bx.h>
#include <bx/spscqueue.h>
#include <bx/thread.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>


#include "bgfx/examples/common/imgui/imgui.h"

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

static int32_t runApiThread(bx::Thread *self, void *userData)
{
	auto args = (ApiThreadArgs *)userData;
	// Initialize bgfx using the native window handle and window resolution.
	bgfx::Init init;
	init.platformData = args->platformData;
	init.resolution.width = args->width;
	init.resolution.height = args->height;
	init.resolution.reset = BGFX_RESET_VSYNC;
	if (!bgfx::init(init))
		return 1;

	// Imgui.
	imguiCreate();

	// Set view 0 to the same dimensions as the window and to clear the color buffer.
	const bgfx::ViewId kClearView = 0;
	bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR);
	bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
	uint32_t width = args->width;
	uint32_t height = args->height;
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


		ImGui::End();

		imguiEndFrame();
		// This dummy draw call is here to make sure that view 0 is cleared if no other draw calls are submitted to view 0.
		bgfx::touch(kClearView);
		// Use debug font to print information about this example.
		bgfx::dbgTextClear();
		
		bgfx::dbgTextPrintf(0, 0, 0x0f, "Press F1 to toggle stats.");
		bgfx::dbgTextPrintf(0, 1, 0x0f, "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");
		bgfx::dbgTextPrintf(80, 1, 0x0f, "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
		bgfx::dbgTextPrintf(80, 2, 0x0f, "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");
		const bgfx::Stats* stats = bgfx::getStats();
		bgfx::dbgTextPrintf(0, 2, 0x0f, "Backbuffer %dW x %dH in pixels, debug text %dW x %dH in characters.", stats->width, stats->height, stats->textWidth, stats->textHeight);
		// Enable stats or debug text.
		bgfx::setDebug(showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);
		// Advance to next frame. Main thread will be kicked to process submitted rendering primitives.
		bgfx::frame();
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
	GLFWwindow *window = glfwCreateWindow(1024, 768, "helloworld multithreaded", nullptr, nullptr);
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
