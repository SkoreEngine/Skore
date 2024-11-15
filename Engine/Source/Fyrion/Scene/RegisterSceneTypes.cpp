#include "Scene.hpp"
#include "GameObject.hpp"
#include "Component/CameraComponent.hpp"
#include "Component/Component.hpp"
#include "Component/EnvironmentComponent.hpp"
#include "Component/LightComponent.hpp"
#include "Component/RenderComponent.hpp"
#include "Component/TransformComponent.hpp"
#include "Fyrion/Engine.hpp"
#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Service/RenderService.hpp"

namespace Fyrion
{
    struct RotateComponent : Component
    {
        FY_BASE_TYPES(Component);

        f32 speed = 1.0;
        f32 rotation = 0;

        TransformComponent* transformComponent = nullptr;

        void OnStart() override
        {
            EnableUpdate();
            transformComponent = gameObject->GetComponent<TransformComponent>();
        }

        void OnUpdate() override
        {
            if (transformComponent)
            {
                transformComponent->SetRotation(Math::Normalize(transformComponent->GetRotation() * Quat(Vec3{0, static_cast<f32>(Engine::DeltaTime()) * speed, 0})));
            }
        }

        static void RegisterType(NativeTypeHandler<RotateComponent>& type)
        {
            type.Field<&RotateComponent::speed>("speed").Attribute<UIProperty>();
            type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
        }
    };

    void RegisterSceneTypes()
    {
        Registry::Type<Scene>();
        Registry::Type<GameObject>();
        Registry::Type<Service>();
        Registry::Type<Component>();

        //components
        Registry::Type<TransformComponent>();
        Registry::Type<RenderComponent>();
        Registry::Type<LightComponent>();
        Registry::Type<EnvironmentComponent>();
        Registry::Type<CameraComponent>();

        Registry::Type<RotateComponent>();

        //services
        Registry::Type<RenderService>();
    }
}
