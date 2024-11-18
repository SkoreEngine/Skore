#include "Scene.hpp"
#include "GameObject.hpp"
#include "Component/CameraComponent.hpp"
#include "Component/Component.hpp"
#include "Component/EnvironmentComponent.hpp"
#include "Component/LightComponent.hpp"
#include "Component/RenderComponent.hpp"
#include "Component/TransformComponent.hpp"
#include "Component/Physics/BoxColliderComponent.hpp"
#include "Component/Physics/CharacterComponent.hpp"
#include "Component/Physics/RigidBodyComponent.hpp"
#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{
    void RegisterSceneTypes()
    {
        Registry::Type<Scene>();
        Registry::Type<GameObject>();
        Registry::Type<Proxy>();
        Registry::Type<Component>();

        //components
        Registry::Type<TransformComponent>();
        Registry::Type<RenderComponent>();
        Registry::Type<LightComponent>();
        Registry::Type<EnvironmentComponent>();
        Registry::Type<CameraComponent>();
        Registry::Type<BoxColliderComponent>();
        Registry::Type<RigidBodyComponent>();
        Registry::Type<CharacterComponent>();
    }
}
