#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	enum class Key
	{
		None = 0,
		Space,
		Apostrophe,
		Comma,
		Minus,
		Period,
		Slash,
		Num0,
		Num1,
		Num2,
		Num3,
		Num4,
		Num5,
		Num6,
		Num7,
		Num8,
		Num9,
		Semicolon,
		Equal,
		A,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,
		LeftBracket,
		Backslash,
		RightBracket,
		GraveAccent,
		World1,
		World2,
		Escape,
		Enter,
		Tab,
		Backspace,
		Insert,
		Delete,
		Right,
		Left,
		Down,
		Up,
		PageUp,
		PageDown,
		Home,
		End,
		CapsLock,
		ScrollLock,
		NumLock,
		PrintScreen,
		Pause,
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
		F13,
		F14,
		F15,
		F16,
		F17,
		F18,
		F19,
		F20,
		F21,
		F22,
		F23,
		F24,
		F25,
		Keypad0,
		Keypad1,
		Keypad2,
		Keypad3,
		Keypad4,
		Keypad5,
		Keypad6,
		Keypad7,
		Keypad8,
		Keypad9,
		KeypadDecimal,
		KeypadDivide,
		KeypadMultiply,
		KeypadSubtract,
		KeypadAdd,
		KeypadEnter,
		KeypadEqual,
		LeftShift,
		LeftCtrl,
		LeftAlt,
		LeftSuper,
		RightShift,
		RightCtrl,
		RightAlt,
		RightSuper,
		Menu,
		MAX
	};

	enum class MouseButton
	{
		Left   = 1,
		Middle = 2,
		Right  = 3,
		X1     = 4,
		X2     = 5,
		MAX
	};

	enum class MouseCursor
	{
		None = -1,
		Arrow,
		TextInput,
		ResizeAll,
		ResizeNS,
		ResizeWE,
		ResizeNESW,
		ResizeNWSE,
		Hand,
		NotAllowed,
		COUT
	};

	enum class InputSourceType : u8
	{
		Keyboard,
		Gamepad,
		MouseClick,
		MouseMove
	};

	enum class CursorLockMode : u8
	{
		Free,
		Locked,
	};

	struct Shortcut
	{
		bool ctrl{};
		bool shift{};
		bool alt{};
		Key  presKey{};
	};
}
