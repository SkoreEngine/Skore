#pragma once

#include "InputTypes.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"


namespace Skore::Input
{
    SK_API bool IsKeyDown(Key key);
    SK_API bool IsKeyPressed(Key key);
    SK_API bool IsKeyReleased(Key key);
    SK_API bool IsMouseDown(MouseButton mouseButton);
    SK_API bool IsMouseClicked(MouseButton mouseButton);
    SK_API bool IsMouseReleased(MouseButton mouseButton);
    SK_API Vec2 GetMousePosition();
    SK_API bool IsMouseMoving();
    SK_API void SetCursorEnabled(bool enabled);

    SK_API void RegisterInputEvent(InputEvent inputEvent);
}
