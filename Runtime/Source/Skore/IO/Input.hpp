#pragma once

#include "InputTypes.hpp"
#include "Skore/Core/Math.hpp"


namespace Skore
{
	struct SK_API Input
	{
		static bool           IsKeyDown(Key key);
		static bool           IsKeyPressed(Key key);
		static bool           IsKeyReleased(Key key);
		static bool           IsMouseDown(MouseButton mouseButton);
		static bool           IsAnyMouseDown();
		static bool           IsMouseClicked(MouseButton mouseButton);
		static bool           IsMouseReleased(MouseButton mouseButton);
		static Vec2           GetMousePosition();
		static Vec2           GetMouseWheel();
		static bool           IsMouseMoving();
		static void           SetCursorLockMode(CursorLockMode cursorLockMode);
		static CursorLockMode GetCursorLockMode();
		static Vec2           GetMouseAxis();
		static void           DisableInputs(bool disable);
	};
}
