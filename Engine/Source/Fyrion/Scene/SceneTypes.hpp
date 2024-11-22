#pragma once
#include "Fyrion/Common.hpp"
#include "Fyrion/Core/Array.hpp"


namespace Fyrion
{
    class Scene;

    namespace SceneEventType
    {
        constexpr static i32 TransformChanged = 1000;
    }

    struct ComponentDesc
    {
        bool          allowMultiple = true;
        Array<TypeID> dependencies{};
    };

    struct SceneEventDesc
    {
        i64     type;
        VoidPtr eventData;
    };

    struct SceneSettings
    {
        Scene* defaultScene;

        static void RegisterType(NativeTypeHandler<SceneSettings>& type);
    };
}
