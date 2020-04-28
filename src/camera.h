/*
 * Copyright 2013 Dario Manesku. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#ifndef CAMERA_H_HEADER_GUARD
#define CAMERA_H_HEADER_GUARD

struct MouseButton
{
	enum Enum
	{
		
		Left,
		Right,
		Middle,
		

		Count
	};
};

struct Modifier
{
	enum Enum
	{
		None = 0,
		LeftAlt = 0x01,
		RightAlt = 0x02,
		LeftCtrl = 0x04,
		RightCtrl = 0x08,
		LeftShift = 0x10,
		RightShift = 0x20,
		LeftMeta = 0x40,
		RightMeta = 0x80,
	};
};

struct Key
{
	enum Enum
	{
		None = 0,
		Esc,
		Return,
		Tab,
		Space,
		Backspace,
		Up,
		Down,
		Left,
		Right,
		Insert,
		Delete,
		Home,
		End,
		PageUp,
		PageDown,
		Print,
		Plus,
		Minus,
		LeftBracket,
		RightBracket,
		Semicolon,
		Quote,
		Comma,
		Period,
		Slash,
		Backslash,
		Tilde,
		F1,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,
		NumPad0,
		NumPad1,
		NumPad2,
		NumPad3,
		NumPad4,
		NumPad5,
		NumPad6,
		NumPad7,
		NumPad8,
		NumPad9,
		Key0,
		Key1,
		Key2,
		Key3,
		Key4,
		Key5,
		Key6,
		Key7,
		Key8,
		Key9,
		KeyA,
		KeyB,
		KeyC,
		KeyD,
		KeyE,
		KeyF,
		KeyG,
		KeyH,
		KeyI,
		KeyJ,
		KeyK,
		KeyL,
		KeyM,
		KeyN,
		KeyO,
		KeyP,
		KeyQ,
		KeyR,
		KeyS,
		KeyT,
		KeyU,
		KeyV,
		KeyW,
		KeyX,
		KeyY,
		KeyZ,

		GamepadA,
		GamepadB,
		GamepadX,
		GamepadY,
		GamepadThumbL,
		GamepadThumbR,
		GamepadShoulderL,
		GamepadShoulderR,
		GamepadUp,
		GamepadDown,
		GamepadLeft,
		GamepadRight,
		GamepadBack,
		GamepadStart,
		GamepadGuide,

		Count
	};
};

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

#define CAMERA_KEY_FORWARD   UINT8_C(0x01)
#define CAMERA_KEY_BACKWARD  UINT8_C(0x02)
#define CAMERA_KEY_LEFT      UINT8_C(0x04)
#define CAMERA_KEY_RIGHT     UINT8_C(0x08)
#define CAMERA_KEY_UP        UINT8_C(0x10)
#define CAMERA_KEY_DOWN      UINT8_C(0x20)

///
void cameraCreate();

///
void cameraDestroy();

///
void cameraSetPosition(const bx::Vec3& _pos);

///
void cameraSetHorizontalAngle(float _horizontalAngle);

///
void cameraSetVerticalAngle(float _verticalAngle);

///
void cameraSetKeyState(uint8_t _key, bool _down);

///
void cameraGetViewMtx(float* _viewMtx);

///
bx::Vec3 cameraGetPosition();

///
bx::Vec3 cameraGetAt();

///
void cameraUpdate(float _deltaTime, MouseState& _mouseState);

#endif // CAMERA_H_HEADER_GUARD
