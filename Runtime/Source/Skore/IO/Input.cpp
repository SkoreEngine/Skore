// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Input.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/FixedArray.hpp"

#include <SDL3/SDL.h>

#include "Skore/Events.hpp"
#include "Skore/Core/Logger.hpp"

namespace Skore
{
	SDL_Window* GraphicsGetWindow();
	Key         FromSDL(u32 key);

	namespace
	{
		bool                                                 inputDisabled = false;
		FixedArray<bool, static_cast<u64>(Key::MAX)>         keyState;
		FixedArray<bool, static_cast<u64>(Key::MAX)>         prevKeyState;
		FixedArray<bool, static_cast<u64>(MouseButton::MAX)> mouseButtonState;
		FixedArray<bool, static_cast<u64>(MouseButton::MAX)> prevMouseButtonState;
		Vec2                                                 mousePosition;
		Vec2                                                 mouseRelativePosition;
		Vec2                                                 mousePosBeforeHideCursor = Vec2{0.0f, 0.0f};
		bool                                                 mouseMoved = false;
		CursorLockMode                                       currentCursorLockMode = CursorLockMode::None;
		FixedArray<u32, 512>                                 keyMap;

		Logger& logger = Logger::GetLogger("Skore::Input");
	}

	bool Input::IsKeyDown(Key key)
	{
		return keyState[static_cast<u64>(key)];
	}

	bool Input::IsKeyPressed(Key key)
	{
		return keyState[static_cast<size_t>(key)] && !prevKeyState[static_cast<size_t>(key)];
	}

	bool Input::IsKeyReleased(Key key)
	{
		return !keyState[static_cast<size_t>(key)];
	}

	bool Input::IsMouseDown(MouseButton mouseButton)
	{
		return mouseButtonState[static_cast<size_t>(mouseButton)];
	}

	bool Input::IsMouseClicked(MouseButton mouseButton)
	{
		return mouseButtonState[static_cast<size_t>(mouseButton)] && !prevMouseButtonState[static_cast<size_t>(mouseButton)];
	}

	bool Input::IsMouseReleased(MouseButton mouseButton)
	{
		return !mouseButtonState[static_cast<size_t>(mouseButton)];
	}

	Vec2 Input::GetMousePosition()
	{
		return mousePosition;
	}

	bool Input::IsMouseMoving()
	{
		return mouseMoved;
	}

	void Input::SetCursorLockMode(CursorLockMode cursorLockMode)
	{
		if (currentCursorLockMode == cursorLockMode) return;
		currentCursorLockMode = cursorLockMode;

		SDL_Window* window = GraphicsGetWindow();
		switch (cursorLockMode)
		{
			case CursorLockMode::None:
			{
				SDL_SetWindowRelativeMouseMode(window, false);
				SDL_WarpMouseGlobal(mousePosBeforeHideCursor.x, mousePosBeforeHideCursor.y);
				break;
			}
			case CursorLockMode::Locked:
			{
				SDL_GetGlobalMouseState(&mousePosBeforeHideCursor.x, &mousePosBeforeHideCursor.y);
				SDL_SetWindowRelativeMouseMode(window, true);
				break;
			}
		}
	}

	CursorLockMode Input::GetCursorLockMode()
	{
		return currentCursorLockMode;
	}

	Vec2 Input::GetMouseAxis()
	{
		return mouseRelativePosition;
	}

	void Input::DisableInputs(bool disable)
	{
		inputDisabled = disable;
	}

	void InputHandlerEvents(SDL_Event* event)
	{
		if (inputDisabled) return;

		switch (event->type)
		{
			case SDL_EVENT_KEY_UP:
			case SDL_EVENT_KEY_DOWN:
				keyState[static_cast<usize>(FromSDL(event->key.scancode))] = event->type == SDL_EVENT_KEY_DOWN;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				mouseButtonState[event->button.button] = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				mousePosition = Vec2{event->motion.x, event->motion.y};
				mouseRelativePosition = Vec2{event->motion.xrel, event->motion.yrel};
				mouseMoved = true;
				break;
		}
	}

	void InputOnEndFrame()
	{
		mouseRelativePosition = {};
		mouseMoved = false;

		for (int i = 0; i < static_cast<u64>(Key::MAX); ++i)
		{
			prevKeyState[i] = keyState[i];
		}

		for (int i = 0; i < static_cast<u64>(MouseButton::MAX); ++i)
		{
			prevMouseButtonState[i] = mouseButtonState[i];
		}
	}

	Key FromSDL(u32 key)
	{
		return static_cast<Key>(keyMap[key]);
	}

	void MapKeys()
	{
		// Initialize all keys to None first to handle unmapped scancodes
		for (int i = 0; i < 512; i++)
		{
			keyMap[i] = static_cast<u32>(Key::None);
		}

		// Map alphabetical keys
		keyMap[SDL_SCANCODE_A] = static_cast<u32>(Key::A);
		keyMap[SDL_SCANCODE_B] = static_cast<u32>(Key::B);
		keyMap[SDL_SCANCODE_C] = static_cast<u32>(Key::C);
		keyMap[SDL_SCANCODE_D] = static_cast<u32>(Key::D);
		keyMap[SDL_SCANCODE_E] = static_cast<u32>(Key::E);
		keyMap[SDL_SCANCODE_F] = static_cast<u32>(Key::F);
		keyMap[SDL_SCANCODE_G] = static_cast<u32>(Key::G);
		keyMap[SDL_SCANCODE_H] = static_cast<u32>(Key::H);
		keyMap[SDL_SCANCODE_I] = static_cast<u32>(Key::I);
		keyMap[SDL_SCANCODE_J] = static_cast<u32>(Key::J);
		keyMap[SDL_SCANCODE_K] = static_cast<u32>(Key::K);
		keyMap[SDL_SCANCODE_L] = static_cast<u32>(Key::L);
		keyMap[SDL_SCANCODE_M] = static_cast<u32>(Key::M);
		keyMap[SDL_SCANCODE_N] = static_cast<u32>(Key::N);
		keyMap[SDL_SCANCODE_O] = static_cast<u32>(Key::O);
		keyMap[SDL_SCANCODE_P] = static_cast<u32>(Key::P);
		keyMap[SDL_SCANCODE_Q] = static_cast<u32>(Key::Q);
		keyMap[SDL_SCANCODE_R] = static_cast<u32>(Key::R);
		keyMap[SDL_SCANCODE_S] = static_cast<u32>(Key::S);
		keyMap[SDL_SCANCODE_T] = static_cast<u32>(Key::T);
		keyMap[SDL_SCANCODE_U] = static_cast<u32>(Key::U);
		keyMap[SDL_SCANCODE_V] = static_cast<u32>(Key::V);
		keyMap[SDL_SCANCODE_W] = static_cast<u32>(Key::W);
		keyMap[SDL_SCANCODE_X] = static_cast<u32>(Key::X);
		keyMap[SDL_SCANCODE_Y] = static_cast<u32>(Key::Y);
		keyMap[SDL_SCANCODE_Z] = static_cast<u32>(Key::Z);

		// Map numeric keys
		keyMap[SDL_SCANCODE_0] = static_cast<u32>(Key::Num0);
		keyMap[SDL_SCANCODE_1] = static_cast<u32>(Key::Num1);
		keyMap[SDL_SCANCODE_2] = static_cast<u32>(Key::Num2);
		keyMap[SDL_SCANCODE_3] = static_cast<u32>(Key::Num3);
		keyMap[SDL_SCANCODE_4] = static_cast<u32>(Key::Num4);
		keyMap[SDL_SCANCODE_5] = static_cast<u32>(Key::Num5);
		keyMap[SDL_SCANCODE_6] = static_cast<u32>(Key::Num6);
		keyMap[SDL_SCANCODE_7] = static_cast<u32>(Key::Num7);
		keyMap[SDL_SCANCODE_8] = static_cast<u32>(Key::Num8);
		keyMap[SDL_SCANCODE_9] = static_cast<u32>(Key::Num9);

		// Map function keys
		keyMap[SDL_SCANCODE_F1] = static_cast<u32>(Key::F1);
		keyMap[SDL_SCANCODE_F2] = static_cast<u32>(Key::F2);
		keyMap[SDL_SCANCODE_F3] = static_cast<u32>(Key::F3);
		keyMap[SDL_SCANCODE_F4] = static_cast<u32>(Key::F4);
		keyMap[SDL_SCANCODE_F5] = static_cast<u32>(Key::F5);
		keyMap[SDL_SCANCODE_F6] = static_cast<u32>(Key::F6);
		keyMap[SDL_SCANCODE_F7] = static_cast<u32>(Key::F7);
		keyMap[SDL_SCANCODE_F8] = static_cast<u32>(Key::F8);
		keyMap[SDL_SCANCODE_F9] = static_cast<u32>(Key::F9);
		keyMap[SDL_SCANCODE_F10] = static_cast<u32>(Key::F10);
		keyMap[SDL_SCANCODE_F11] = static_cast<u32>(Key::F11);
		keyMap[SDL_SCANCODE_F12] = static_cast<u32>(Key::F12);
		keyMap[SDL_SCANCODE_F13] = static_cast<u32>(Key::F13);
		keyMap[SDL_SCANCODE_F14] = static_cast<u32>(Key::F14);
		keyMap[SDL_SCANCODE_F15] = static_cast<u32>(Key::F15);
		keyMap[SDL_SCANCODE_F16] = static_cast<u32>(Key::F16);
		keyMap[SDL_SCANCODE_F17] = static_cast<u32>(Key::F17);
		keyMap[SDL_SCANCODE_F18] = static_cast<u32>(Key::F18);
		keyMap[SDL_SCANCODE_F19] = static_cast<u32>(Key::F19);
		keyMap[SDL_SCANCODE_F20] = static_cast<u32>(Key::F20);
		keyMap[SDL_SCANCODE_F21] = static_cast<u32>(Key::F21);
		keyMap[SDL_SCANCODE_F22] = static_cast<u32>(Key::F22);
		keyMap[SDL_SCANCODE_F23] = static_cast<u32>(Key::F23);
		keyMap[SDL_SCANCODE_F24] = static_cast<u32>(Key::F24);
		// F25 doesn't have a direct SDL equivalent, so we don't map it

		// Map special keys
		keyMap[SDL_SCANCODE_SPACE] = static_cast<u32>(Key::Space);
		keyMap[SDL_SCANCODE_APOSTROPHE] = static_cast<u32>(Key::Apostrophe);
		keyMap[SDL_SCANCODE_COMMA] = static_cast<u32>(Key::Comma);
		keyMap[SDL_SCANCODE_MINUS] = static_cast<u32>(Key::Minus);
		keyMap[SDL_SCANCODE_PERIOD] = static_cast<u32>(Key::Period);
		keyMap[SDL_SCANCODE_SLASH] = static_cast<u32>(Key::Slash);
		keyMap[SDL_SCANCODE_SEMICOLON] = static_cast<u32>(Key::Semicolon);
		keyMap[SDL_SCANCODE_EQUALS] = static_cast<u32>(Key::Equal);
		keyMap[SDL_SCANCODE_LEFTBRACKET] = static_cast<u32>(Key::LeftBracket);
		keyMap[SDL_SCANCODE_BACKSLASH] = static_cast<u32>(Key::Backslash);
		keyMap[SDL_SCANCODE_RIGHTBRACKET] = static_cast<u32>(Key::RightBracket);
		keyMap[SDL_SCANCODE_GRAVE] = static_cast<u32>(Key::GraveAccent);
		// World1 and World2 don't have direct SDL equivalents in standard keyboards
		// They might correspond to international keys, but the mapping is not clear

		// Map control keys
		keyMap[SDL_SCANCODE_ESCAPE] = static_cast<u32>(Key::Escape);
		keyMap[SDL_SCANCODE_RETURN] = static_cast<u32>(Key::Enter);
		keyMap[SDL_SCANCODE_TAB] = static_cast<u32>(Key::Tab);
		keyMap[SDL_SCANCODE_BACKSPACE] = static_cast<u32>(Key::Backspace);
		keyMap[SDL_SCANCODE_INSERT] = static_cast<u32>(Key::Insert);
		keyMap[SDL_SCANCODE_DELETE] = static_cast<u32>(Key::Delete);
		keyMap[SDL_SCANCODE_RIGHT] = static_cast<u32>(Key::Right);
		keyMap[SDL_SCANCODE_LEFT] = static_cast<u32>(Key::Left);
		keyMap[SDL_SCANCODE_DOWN] = static_cast<u32>(Key::Down);
		keyMap[SDL_SCANCODE_UP] = static_cast<u32>(Key::Up);
		keyMap[SDL_SCANCODE_PAGEUP] = static_cast<u32>(Key::PageUp);
		keyMap[SDL_SCANCODE_PAGEDOWN] = static_cast<u32>(Key::PageDown);
		keyMap[SDL_SCANCODE_HOME] = static_cast<u32>(Key::Home);
		keyMap[SDL_SCANCODE_END] = static_cast<u32>(Key::End);
		keyMap[SDL_SCANCODE_CAPSLOCK] = static_cast<u32>(Key::CapsLock);
		keyMap[SDL_SCANCODE_SCROLLLOCK] = static_cast<u32>(Key::ScrollLock);
		keyMap[SDL_SCANCODE_NUMLOCKCLEAR] = static_cast<u32>(Key::NumLock);
		keyMap[SDL_SCANCODE_PRINTSCREEN] = static_cast<u32>(Key::PrintScreen);
		keyMap[SDL_SCANCODE_PAUSE] = static_cast<u32>(Key::Pause);

		// Map keypad keys
		keyMap[SDL_SCANCODE_KP_0] = static_cast<u32>(Key::Keypad0);
		keyMap[SDL_SCANCODE_KP_1] = static_cast<u32>(Key::Keypad1);
		keyMap[SDL_SCANCODE_KP_2] = static_cast<u32>(Key::Keypad2);
		keyMap[SDL_SCANCODE_KP_3] = static_cast<u32>(Key::Keypad3);
		keyMap[SDL_SCANCODE_KP_4] = static_cast<u32>(Key::Keypad4);
		keyMap[SDL_SCANCODE_KP_5] = static_cast<u32>(Key::Keypad5);
		keyMap[SDL_SCANCODE_KP_6] = static_cast<u32>(Key::Keypad6);
		keyMap[SDL_SCANCODE_KP_7] = static_cast<u32>(Key::Keypad7);
		keyMap[SDL_SCANCODE_KP_8] = static_cast<u32>(Key::Keypad8);
		keyMap[SDL_SCANCODE_KP_9] = static_cast<u32>(Key::Keypad9);
		keyMap[SDL_SCANCODE_KP_PERIOD] = static_cast<u32>(Key::KeypadDecimal);
		keyMap[SDL_SCANCODE_KP_DIVIDE] = static_cast<u32>(Key::KeypadDivide);
		keyMap[SDL_SCANCODE_KP_MULTIPLY] = static_cast<u32>(Key::KeypadMultiply);
		keyMap[SDL_SCANCODE_KP_MINUS] = static_cast<u32>(Key::KeypadSubtract);
		keyMap[SDL_SCANCODE_KP_PLUS] = static_cast<u32>(Key::KeypadAdd);
		keyMap[SDL_SCANCODE_KP_ENTER] = static_cast<u32>(Key::KeypadEnter);
		keyMap[SDL_SCANCODE_KP_EQUALS] = static_cast<u32>(Key::KeypadEqual);

		// Map modifier keys
		keyMap[SDL_SCANCODE_LSHIFT] = static_cast<u32>(Key::LeftShift);
		keyMap[SDL_SCANCODE_LCTRL] = static_cast<u32>(Key::LeftCtrl);
		keyMap[SDL_SCANCODE_LALT] = static_cast<u32>(Key::LeftAlt);
		keyMap[SDL_SCANCODE_LGUI] = static_cast<u32>(Key::LeftSuper);
		keyMap[SDL_SCANCODE_RSHIFT] = static_cast<u32>(Key::RightShift);
		keyMap[SDL_SCANCODE_RCTRL] = static_cast<u32>(Key::RightCtrl);
		keyMap[SDL_SCANCODE_RALT] = static_cast<u32>(Key::RightAlt);
		keyMap[SDL_SCANCODE_RGUI] = static_cast<u32>(Key::RightSuper);
		keyMap[SDL_SCANCODE_MENU] = static_cast<u32>(Key::Menu);
	}

	void InputInit()
	{
		keyState = {};
		prevKeyState = {};
		prevMouseButtonState = {};

		MapKeys();

		Event::Bind<OnEndFrame, InputOnEndFrame>();
	}
}
