#include "Scene.hpp"
#include "GameObject.hpp"
#include "Component/CameraComponent.hpp"
#include "Component/Component.hpp"
#include "Component/EnvironmentComponent.hpp"
#include "Component/LightComponent.hpp"
#include "Component/RenderComponent.hpp"
#include "Component/TransformComponent.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Service/RenderService.hpp"

namespace Fyrion
{
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

        //services
        Registry::Type<RenderService>();


        //enums
        auto projection = Registry::Type<Projection>();
        projection.Value<Projection::Perspective>("Perspective");
        projection.Value<Projection::Orthogonal>("Orthogonal");
    }
}
