#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Physics/PhysicsTypes.hpp"
#include "Skore/Scene/SceneTypes.hpp"

namespace Skore
{
    class GameObject;

    class SK_API Component : public Object
    {
    public:
        ~Component() override;

        friend class GameObject;

        virtual void OnStart() {}
        virtual void OnDestroy() {}
        virtual void OnChange() {}
        virtual void OnUpdate() {}

        virtual void ProcessEvent(const SceneEventDesc& event) {}
        virtual void CollectShapes(Array<BodyShapeBuilder>& shapes) {}

        void EnableUpdate();
        void DisableUpdate();

        GameObject*  gameObject = nullptr;
        TypeHandler* typeHandler = nullptr;
        UUID         uuid = {};
        Component*   instance = nullptr;

        static void RegisterType(NativeTypeHandler<Component>& type);

    private:
        bool updateEnabled = false;
    };
}
