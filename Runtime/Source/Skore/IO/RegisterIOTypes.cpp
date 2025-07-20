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


#include "FileTypes.hpp"
#include "InputTypes.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterKeyEnum()
	{
		auto keyEnum = Reflection::Type<Key>();
		
		// Register each key value
		keyEnum.Value<Key::None>("None");
		keyEnum.Value<Key::Space>("Space");
		keyEnum.Value<Key::Apostrophe>("Apostrophe");
		keyEnum.Value<Key::Comma>("Comma");
		keyEnum.Value<Key::Minus>("Minus");
		keyEnum.Value<Key::Period>("Period");
		keyEnum.Value<Key::Slash>("Slash");
		keyEnum.Value<Key::Num0>("Num0");
		keyEnum.Value<Key::Num1>("Num1");
		keyEnum.Value<Key::Num2>("Num2");
		keyEnum.Value<Key::Num3>("Num3");
		keyEnum.Value<Key::Num4>("Num4");
		keyEnum.Value<Key::Num5>("Num5");
		keyEnum.Value<Key::Num6>("Num6");
		keyEnum.Value<Key::Num7>("Num7");
		keyEnum.Value<Key::Num8>("Num8");
		keyEnum.Value<Key::Num9>("Num9");
		keyEnum.Value<Key::Semicolon>("Semicolon");
		keyEnum.Value<Key::Equal>("Equal");
		keyEnum.Value<Key::A>("A");
		keyEnum.Value<Key::B>("B");
		keyEnum.Value<Key::C>("C");
		keyEnum.Value<Key::D>("D");
		keyEnum.Value<Key::E>("E");
		keyEnum.Value<Key::F>("F");
		keyEnum.Value<Key::G>("G");
		keyEnum.Value<Key::H>("H");
		keyEnum.Value<Key::I>("I");
		keyEnum.Value<Key::J>("J");
		keyEnum.Value<Key::K>("K");
		keyEnum.Value<Key::L>("L");
		keyEnum.Value<Key::M>("M");
		keyEnum.Value<Key::N>("N");
		keyEnum.Value<Key::O>("O");
		keyEnum.Value<Key::P>("P");
		keyEnum.Value<Key::Q>("Q");
		keyEnum.Value<Key::R>("R");
		keyEnum.Value<Key::S>("S");
		keyEnum.Value<Key::T>("T");
		keyEnum.Value<Key::U>("U");
		keyEnum.Value<Key::V>("V");
		keyEnum.Value<Key::W>("W");
		keyEnum.Value<Key::X>("X");
		keyEnum.Value<Key::Y>("Y");
		keyEnum.Value<Key::Z>("Z");
		keyEnum.Value<Key::LeftBracket>("LeftBracket");
		keyEnum.Value<Key::Backslash>("Backslash");
		keyEnum.Value<Key::RightBracket>("RightBracket");
		keyEnum.Value<Key::GraveAccent>("GraveAccent");
		keyEnum.Value<Key::World1>("World1");
		keyEnum.Value<Key::World2>("World2");
		keyEnum.Value<Key::Escape>("Escape");
		keyEnum.Value<Key::Enter>("Enter");
		keyEnum.Value<Key::Tab>("Tab");
		keyEnum.Value<Key::Backspace>("Backspace");
		keyEnum.Value<Key::Insert>("Insert");
		keyEnum.Value<Key::Delete>("Delete");
		keyEnum.Value<Key::Right>("Right");
		keyEnum.Value<Key::Left>("Left");
		keyEnum.Value<Key::Down>("Down");
		keyEnum.Value<Key::Up>("Up");
		keyEnum.Value<Key::PageUp>("PageUp");
		keyEnum.Value<Key::PageDown>("PageDown");
		keyEnum.Value<Key::Home>("Home");
		keyEnum.Value<Key::End>("End");
		keyEnum.Value<Key::CapsLock>("CapsLock");
		keyEnum.Value<Key::ScrollLock>("ScrollLock");
		keyEnum.Value<Key::NumLock>("NumLock");
		keyEnum.Value<Key::PrintScreen>("PrintScreen");
		keyEnum.Value<Key::Pause>("Pause");
		keyEnum.Value<Key::F1>("F1");
		keyEnum.Value<Key::F2>("F2");
		keyEnum.Value<Key::F3>("F3");
		keyEnum.Value<Key::F4>("F4");
		keyEnum.Value<Key::F5>("F5");
		keyEnum.Value<Key::F6>("F6");
		keyEnum.Value<Key::F7>("F7");
		keyEnum.Value<Key::F8>("F8");
		keyEnum.Value<Key::F9>("F9");
		keyEnum.Value<Key::F10>("F10");
		keyEnum.Value<Key::F11>("F11");
		keyEnum.Value<Key::F12>("F12");
		keyEnum.Value<Key::F13>("F13");
		keyEnum.Value<Key::F14>("F14");
		keyEnum.Value<Key::F15>("F15");
		keyEnum.Value<Key::F16>("F16");
		keyEnum.Value<Key::F17>("F17");
		keyEnum.Value<Key::F18>("F18");
		keyEnum.Value<Key::F19>("F19");
		keyEnum.Value<Key::F20>("F20");
		keyEnum.Value<Key::F21>("F21");
		keyEnum.Value<Key::F22>("F22");
		keyEnum.Value<Key::F23>("F23");
		keyEnum.Value<Key::F24>("F24");
		keyEnum.Value<Key::F25>("F25");
		keyEnum.Value<Key::Keypad0>("Keypad0");
		keyEnum.Value<Key::Keypad1>("Keypad1");
		keyEnum.Value<Key::Keypad2>("Keypad2");
		keyEnum.Value<Key::Keypad3>("Keypad3");
		keyEnum.Value<Key::Keypad4>("Keypad4");
		keyEnum.Value<Key::Keypad5>("Keypad5");
		keyEnum.Value<Key::Keypad6>("Keypad6");
		keyEnum.Value<Key::Keypad7>("Keypad7");
		keyEnum.Value<Key::Keypad8>("Keypad8");
		keyEnum.Value<Key::Keypad9>("Keypad9");
		keyEnum.Value<Key::KeypadDecimal>("KeypadDecimal");
		keyEnum.Value<Key::KeypadDivide>("KeypadDivide");
		keyEnum.Value<Key::KeypadMultiply>("KeypadMultiply");
		keyEnum.Value<Key::KeypadSubtract>("KeypadSubtract");
		keyEnum.Value<Key::KeypadAdd>("KeypadAdd");
		keyEnum.Value<Key::KeypadEnter>("KeypadEnter");
		keyEnum.Value<Key::KeypadEqual>("KeypadEqual");
		keyEnum.Value<Key::LeftShift>("LeftShift");
		keyEnum.Value<Key::LeftCtrl>("LeftCtrl");
		keyEnum.Value<Key::LeftAlt>("LeftAlt");
		keyEnum.Value<Key::LeftSuper>("LeftSuper");
		keyEnum.Value<Key::RightShift>("RightShift");
		keyEnum.Value<Key::RightCtrl>("RightCtrl");
		keyEnum.Value<Key::RightAlt>("RightAlt");
		keyEnum.Value<Key::RightSuper>("RightSuper");
		keyEnum.Value<Key::Menu>("Menu");
		keyEnum.Value<Key::MAX>("MAX");
	}
	
	void RegisterMouseButtonEnum()
	{
		auto mouseButtonEnum = Reflection::Type<MouseButton>();
		
		mouseButtonEnum.Value<MouseButton::Left>("Left");
		mouseButtonEnum.Value<MouseButton::Middle>("Middle");
		mouseButtonEnum.Value<MouseButton::Right>("Right");
		mouseButtonEnum.Value<MouseButton::X1>("X1");
		mouseButtonEnum.Value<MouseButton::X2>("X2");
		mouseButtonEnum.Value<MouseButton::MAX>("MAX");
	}
	
	void RegisterMouseCursorEnum()
	{
		auto mouseCursorEnum = Reflection::Type<MouseCursor>();
		
		mouseCursorEnum.Value<MouseCursor::None>("None");
		mouseCursorEnum.Value<MouseCursor::Arrow>("Arrow");
		mouseCursorEnum.Value<MouseCursor::TextInput>("TextInput");
		mouseCursorEnum.Value<MouseCursor::ResizeAll>("ResizeAll");
		mouseCursorEnum.Value<MouseCursor::ResizeNS>("ResizeNS");
		mouseCursorEnum.Value<MouseCursor::ResizeWE>("ResizeWE");
		mouseCursorEnum.Value<MouseCursor::ResizeNESW>("ResizeNESW");
		mouseCursorEnum.Value<MouseCursor::ResizeNWSE>("ResizeNWSE");
		mouseCursorEnum.Value<MouseCursor::Hand>("Hand");
		mouseCursorEnum.Value<MouseCursor::NotAllowed>("NotAllowed");
		mouseCursorEnum.Value<MouseCursor::COUT>("COUT");
	}
	
	void RegisterInputSourceTypeEnum()
	{
		auto inputSourceTypeEnum = Reflection::Type<InputSourceType>();
		
		inputSourceTypeEnum.Value<InputSourceType::Keyboard>("Keyboard");
		inputSourceTypeEnum.Value<InputSourceType::Gamepad>("Gamepad");
		inputSourceTypeEnum.Value<InputSourceType::MouseClick>("MouseClick");
		inputSourceTypeEnum.Value<InputSourceType::MouseMove>("MouseMove");
	}
	

	void RegisterAccessModeEnum()
	{
		auto accessModeEnum = Reflection::Type<AccessMode>();
		
		accessModeEnum.Value<AccessMode::None>("None");
		accessModeEnum.Value<AccessMode::ReadOnly>("ReadOnly");
		accessModeEnum.Value<AccessMode::WriteOnly>("WriteOnly");
		accessModeEnum.Value<AccessMode::ReadAndWrite>("ReadAndWrite");
	}
	
	// Register struct types with their fields
	void RegisterStructTypes()
	{
		// Shortcut struct
		auto shortcutType = Reflection::Type<Shortcut>();
		shortcutType.Field<&Shortcut::ctrl>("ctrl");
		shortcutType.Field<&Shortcut::shift>("shift");
		shortcutType.Field<&Shortcut::alt>("alt");
		shortcutType.Field<&Shortcut::presKey>("presKey");
		
		// FileStatus struct
		auto fileStatusType = Reflection::Type<FileStatus>();
		fileStatusType.Field<&FileStatus::exists>("exists");
		fileStatusType.Field<&FileStatus::isDirectory>("isDirectory");
		fileStatusType.Field<&FileStatus::lastModifiedTime>("lastModifiedTime");
		fileStatusType.Field<&FileStatus::fileSize>("fileSize");
	}

	void RegisterIOTypes()
	{
		// Register enums
		RegisterKeyEnum();
		RegisterMouseButtonEnum();
		RegisterMouseCursorEnum();
		RegisterInputSourceTypeEnum();
		RegisterAccessModeEnum();
		
		// Register structs
		RegisterStructTypes();
	}
}
