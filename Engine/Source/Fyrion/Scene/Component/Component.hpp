#pragma once

#include "Fyrion/Common.hpp"
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

        GameObject* gameObject = nullptr;
        TypeID      typeId = 0;

        static void RegisterType(NativeTypeHandler<Component>& type);
    };
}
