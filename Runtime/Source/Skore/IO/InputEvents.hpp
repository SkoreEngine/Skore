#pragma once

#include "Skore/Core/Event.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/IO/InputTypes.hpp"

namespace Skore
{
	using OnKeyDown     = EventType<"Skore::OnKeyDown"_h,     void(Key key, bool repeat)>;
	using OnKeyUp       = EventType<"Skore::OnKeyUp"_h,       void(Key key)>;
	using OnTextInput   = EventType<"Skore::OnTextInput"_h,   void(StringView text)>;
	using OnMouseMove   = EventType<"Skore::OnMouseMove"_h,   void(Vec2 position)>;
	using OnMouseButton = EventType<"Skore::OnMouseButton"_h, void(MouseButton button, bool pressed)>;
	using OnMouseScroll = EventType<"Skore::OnMouseScroll"_h, void(Vec2 delta)>;
}
