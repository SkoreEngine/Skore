#pragma once

#include "Fyrion/Common.hpp"
#include "Fyrion/Core/UUID.hpp"
#include "Fyrion/Scene/SceneTypes.hpp"

namespace Fyrion
{
    class GameObject;

    class FY_API Component : public Object
    {
    public:
        friend class GameObject;

        virtual void OnStart() {}
        virtual void OnDestroy() {}
        virtual void OnChange() {}

        virtual void ProcessEvent(const SceneEventDesc& event) {}

        GameObject*  gameObject = nullptr;
        TypeHandler* typeHandler = nullptr;
        UUID         uuid = {};
        Component*   instance = nullptr;

        static void RegisterType(NativeTypeHandler<Component>& type);
    };
}
