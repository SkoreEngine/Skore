#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"


namespace Skore
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
