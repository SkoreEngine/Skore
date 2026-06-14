#include "RmlUiManager.hpp"

#include "RenderInterfaceSkore.hpp"
#include "SystemInterfaceSkore.hpp"
#include "FontEngineSkore.hpp"
#include "FileInterfaceSkore.hpp"

#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/IO/Input.hpp"
#include "Skore/IO/InputEvents.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>

namespace Skore
{
	namespace
	{
		RenderInterfaceSkore* renderInterface = nullptr;
		SystemInterfaceSkore* systemInterface = nullptr;
		FontEngineSkore*      fontEngine = nullptr;
		FileInterfaceSkore*   fileInterface = nullptr;

		struct ContextEntry
		{
			Rml::Context* context = nullptr;
			Vec2          offset = {0, 0};
			f32           scale = 1.0f;
		};

		Array<ContextEntry> contexts;

		Rml::Input::KeyIdentifier ToRmlKey(Key key)
		{
			switch (key)
			{
				case Key::Space: return Rml::Input::KI_SPACE;
				case Key::Apostrophe: return Rml::Input::KI_OEM_7;
				case Key::Comma: return Rml::Input::KI_OEM_COMMA;
				case Key::Minus: return Rml::Input::KI_OEM_MINUS;
				case Key::Period: return Rml::Input::KI_OEM_PERIOD;
				case Key::Slash: return Rml::Input::KI_OEM_2;
				case Key::Num0: return Rml::Input::KI_0;
				case Key::Num1: return Rml::Input::KI_1;
				case Key::Num2: return Rml::Input::KI_2;
				case Key::Num3: return Rml::Input::KI_3;
				case Key::Num4: return Rml::Input::KI_4;
				case Key::Num5: return Rml::Input::KI_5;
				case Key::Num6: return Rml::Input::KI_6;
				case Key::Num7: return Rml::Input::KI_7;
				case Key::Num8: return Rml::Input::KI_8;
				case Key::Num9: return Rml::Input::KI_9;
				case Key::Semicolon: return Rml::Input::KI_OEM_1;
				case Key::Equal: return Rml::Input::KI_OEM_PLUS;
				case Key::A: return Rml::Input::KI_A;
				case Key::B: return Rml::Input::KI_B;
				case Key::C: return Rml::Input::KI_C;
				case Key::D: return Rml::Input::KI_D;
				case Key::E: return Rml::Input::KI_E;
				case Key::F: return Rml::Input::KI_F;
				case Key::G: return Rml::Input::KI_G;
				case Key::H: return Rml::Input::KI_H;
				case Key::I: return Rml::Input::KI_I;
				case Key::J: return Rml::Input::KI_J;
				case Key::K: return Rml::Input::KI_K;
				case Key::L: return Rml::Input::KI_L;
				case Key::M: return Rml::Input::KI_M;
				case Key::N: return Rml::Input::KI_N;
				case Key::O: return Rml::Input::KI_O;
				case Key::P: return Rml::Input::KI_P;
				case Key::Q: return Rml::Input::KI_Q;
				case Key::R: return Rml::Input::KI_R;
				case Key::S: return Rml::Input::KI_S;
				case Key::T: return Rml::Input::KI_T;
				case Key::U: return Rml::Input::KI_U;
				case Key::V: return Rml::Input::KI_V;
				case Key::W: return Rml::Input::KI_W;
				case Key::X: return Rml::Input::KI_X;
				case Key::Y: return Rml::Input::KI_Y;
				case Key::Z: return Rml::Input::KI_Z;
				case Key::LeftBracket: return Rml::Input::KI_OEM_4;
				case Key::Backslash: return Rml::Input::KI_OEM_5;
				case Key::RightBracket: return Rml::Input::KI_OEM_6;
				case Key::GraveAccent: return Rml::Input::KI_OEM_3;
				case Key::Escape: return Rml::Input::KI_ESCAPE;
				case Key::Enter: return Rml::Input::KI_RETURN;
				case Key::Tab: return Rml::Input::KI_TAB;
				case Key::Backspace: return Rml::Input::KI_BACK;
				case Key::Insert: return Rml::Input::KI_INSERT;
				case Key::Delete: return Rml::Input::KI_DELETE;
				case Key::Right: return Rml::Input::KI_RIGHT;
				case Key::Left: return Rml::Input::KI_LEFT;
				case Key::Down: return Rml::Input::KI_DOWN;
				case Key::Up: return Rml::Input::KI_UP;
				case Key::PageUp: return Rml::Input::KI_PRIOR;
				case Key::PageDown: return Rml::Input::KI_NEXT;
				case Key::Home: return Rml::Input::KI_HOME;
				case Key::End: return Rml::Input::KI_END;
				case Key::CapsLock: return Rml::Input::KI_CAPITAL;
				case Key::ScrollLock: return Rml::Input::KI_SCROLL;
				case Key::NumLock: return Rml::Input::KI_NUMLOCK;
				case Key::PrintScreen: return Rml::Input::KI_SNAPSHOT;
				case Key::Pause: return Rml::Input::KI_PAUSE;
				case Key::F1: return Rml::Input::KI_F1;
				case Key::F2: return Rml::Input::KI_F2;
				case Key::F3: return Rml::Input::KI_F3;
				case Key::F4: return Rml::Input::KI_F4;
				case Key::F5: return Rml::Input::KI_F5;
				case Key::F6: return Rml::Input::KI_F6;
				case Key::F7: return Rml::Input::KI_F7;
				case Key::F8: return Rml::Input::KI_F8;
				case Key::F9: return Rml::Input::KI_F9;
				case Key::F10: return Rml::Input::KI_F10;
				case Key::F11: return Rml::Input::KI_F11;
				case Key::F12: return Rml::Input::KI_F12;
				case Key::F13: return Rml::Input::KI_F13;
				case Key::F14: return Rml::Input::KI_F14;
				case Key::F15: return Rml::Input::KI_F15;
				case Key::F16: return Rml::Input::KI_F16;
				case Key::F17: return Rml::Input::KI_F17;
				case Key::F18: return Rml::Input::KI_F18;
				case Key::F19: return Rml::Input::KI_F19;
				case Key::F20: return Rml::Input::KI_F20;
				case Key::F21: return Rml::Input::KI_F21;
				case Key::F22: return Rml::Input::KI_F22;
				case Key::F23: return Rml::Input::KI_F23;
				case Key::F24: return Rml::Input::KI_F24;
				case Key::Keypad0: return Rml::Input::KI_NUMPAD0;
				case Key::Keypad1: return Rml::Input::KI_NUMPAD1;
				case Key::Keypad2: return Rml::Input::KI_NUMPAD2;
				case Key::Keypad3: return Rml::Input::KI_NUMPAD3;
				case Key::Keypad4: return Rml::Input::KI_NUMPAD4;
				case Key::Keypad5: return Rml::Input::KI_NUMPAD5;
				case Key::Keypad6: return Rml::Input::KI_NUMPAD6;
				case Key::Keypad7: return Rml::Input::KI_NUMPAD7;
				case Key::Keypad8: return Rml::Input::KI_NUMPAD8;
				case Key::Keypad9: return Rml::Input::KI_NUMPAD9;
				case Key::KeypadDecimal: return Rml::Input::KI_DECIMAL;
				case Key::KeypadDivide: return Rml::Input::KI_DIVIDE;
				case Key::KeypadMultiply: return Rml::Input::KI_MULTIPLY;
				case Key::KeypadSubtract: return Rml::Input::KI_SUBTRACT;
				case Key::KeypadAdd: return Rml::Input::KI_ADD;
				case Key::KeypadEnter: return Rml::Input::KI_NUMPADENTER;
				case Key::KeypadEqual: return Rml::Input::KI_OEM_NEC_EQUAL;
				case Key::LeftShift: return Rml::Input::KI_LSHIFT;
				case Key::LeftCtrl: return Rml::Input::KI_LCONTROL;
				case Key::LeftAlt: return Rml::Input::KI_LMENU;
				case Key::LeftSuper: return Rml::Input::KI_LWIN;
				case Key::RightShift: return Rml::Input::KI_RSHIFT;
				case Key::RightCtrl: return Rml::Input::KI_RCONTROL;
				case Key::RightAlt: return Rml::Input::KI_RMENU;
				case Key::RightSuper: return Rml::Input::KI_RWIN;
				case Key::Menu: return Rml::Input::KI_APPS;
				default: return Rml::Input::KI_UNKNOWN;
			}
		}

		int ToRmlMouseButton(MouseButton button)
		{
			switch (button)
			{
				case MouseButton::Left: return 0;
				case MouseButton::Right: return 1;
				case MouseButton::Middle: return 2;
				case MouseButton::X1: return 3;
				case MouseButton::X2: return 4;
				default: return 0;
			}
		}

		int GetRmlModifiers()
		{
			int modifiers = 0;
			if (Input::IsKeyDown(Key::LeftCtrl) || Input::IsKeyDown(Key::RightCtrl)) modifiers |= Rml::Input::KM_CTRL;
			if (Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift)) modifiers |= Rml::Input::KM_SHIFT;
			if (Input::IsKeyDown(Key::LeftAlt) || Input::IsKeyDown(Key::RightAlt)) modifiers |= Rml::Input::KM_ALT;
			if (Input::IsKeyDown(Key::LeftSuper) || Input::IsKeyDown(Key::RightSuper)) modifiers |= Rml::Input::KM_META;
			return modifiers;
		}

		void OnKeyDownEvent(Key key, bool repeat)
		{
			Rml::Input::KeyIdentifier identifier = ToRmlKey(key);
			int                       modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessKeyDown(identifier, modifiers);
			}
		}

		void OnKeyUpEvent(Key key)
		{
			Rml::Input::KeyIdentifier identifier = ToRmlKey(key);
			int                       modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessKeyUp(identifier, modifiers);
			}
		}

		void OnTextInputEvent(StringView text)
		{
			Rml::String string(text.Data(), text.Size());
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessTextInput(string);
			}
		}

		void OnMouseMoveEvent(Vec2 position)
		{
			int modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				Vec2 local = (position - entry.offset) * entry.scale;
				entry.context->ProcessMouseMove(static_cast<int>(local.x), static_cast<int>(local.y), modifiers);
			}
		}

		void OnMouseButtonEvent(MouseButton button, bool pressed)
		{
			int index = ToRmlMouseButton(button);
			int modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				if (pressed)
				{
					entry.context->ProcessMouseButtonDown(index, modifiers);
				}
				else
				{
					entry.context->ProcessMouseButtonUp(index, modifiers);
				}
			}
		}

		void OnMouseScrollEvent(Vec2 delta)
		{
			int modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessMouseWheel(Rml::Vector2f(-delta.x, -delta.y), modifiers);
			}
		}
	}

	RenderInterfaceSkore* RmlUiManager::GetRenderInterface()
	{
		return renderInterface;
	}

	void RmlUiManager::RegisterContext(Rml::Context* context)
	{
		if (context)
		{
			contexts.EmplaceBack(ContextEntry{context});
		}
	}

	void RmlUiManager::UnregisterContext(Rml::Context* context)
	{
		for (auto it = contexts.begin(); it != contexts.end(); ++it)
		{
			if (it->context == context)
			{
				contexts.Erase(it);
				break;
			}
		}
	}

	void RmlUiManager::SetContextInputTransform(Rml::Context* context, Vec2 offset, f32 scale)
	{
		for (ContextEntry& entry : contexts)
		{
			if (entry.context == context)
			{
				entry.offset = offset;
				entry.scale = scale;
				break;
			}
		}
	}

	void RmlUIInit()
	{
		renderInterface = Alloc<RenderInterfaceSkore>();
		systemInterface = Alloc<SystemInterfaceSkore>();
		fontEngine = Alloc<FontEngineSkore>();
		fileInterface = Alloc<FileInterfaceSkore>();

		Rml::SetRenderInterface(renderInterface);
		Rml::SetSystemInterface(systemInterface);
		Rml::SetFontEngineInterface(fontEngine);
		Rml::SetFileInterface(fileInterface);
		Rml::Initialise();

		Event::Bind<OnKeyDown, OnKeyDownEvent>();
		Event::Bind<OnKeyUp, OnKeyUpEvent>();
		Event::Bind<OnTextInput, OnTextInputEvent>();
		Event::Bind<OnMouseMove, OnMouseMoveEvent>();
		Event::Bind<OnMouseButton, OnMouseButtonEvent>();
		Event::Bind<OnMouseScroll, OnMouseScrollEvent>();
	}

	void RmlUIShutdown()
	{
		Event::Unbind<OnKeyDown, OnKeyDownEvent>();
		Event::Unbind<OnKeyUp, OnKeyUpEvent>();
		Event::Unbind<OnTextInput, OnTextInputEvent>();
		Event::Unbind<OnMouseMove, OnMouseMoveEvent>();
		Event::Unbind<OnMouseButton, OnMouseButtonEvent>();
		Event::Unbind<OnMouseScroll, OnMouseScrollEvent>();

		Rml::Shutdown();

		if (renderInterface)
		{
			renderInterface->Destroy();
			DestroyAndFree(renderInterface);
			renderInterface = nullptr;
		}
		if (systemInterface)
		{
			DestroyAndFree(systemInterface);
			systemInterface = nullptr;
		}
		if (fontEngine)
		{
			DestroyAndFree(fontEngine);
			fontEngine = nullptr;
		}
		if (fileInterface)
		{
			DestroyAndFree(fileInterface);
			fileInterface = nullptr;
		}
	}
}
