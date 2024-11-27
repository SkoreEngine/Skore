#include "Scene.hpp"
#include "GameObject.hpp"
#include "Component/CameraComponent.hpp"
#include "Component/Component.hpp"
#include "Component/EnvironmentComponent.hpp"
#include "Component/LightComponent.hpp"
#include "Component/ReflectionProbe.hpp"
#include "Component/RenderComponent.hpp"
#include "Component/TransformComponent.hpp"
#include "Component/Physics/BoxColliderComponent.hpp"
#include "Component/Physics/CharacterComponent.hpp"
#include "Component/Physics/RigidBodyComponent.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"

namespace Skore
{

    void SceneSettings::RegisterType(NativeTypeHandler<SceneSettings>& type)
    {
        type.Field<&SceneSettings::defaultScene>("defaultScene").Attribute<UIProperty>();
        type.Attribute<Settings>(Settings{
            .path = "Application/Scene Settings",
            .type = GetTypeID<ProjectSettings>(
            )
        });
    }


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
        Registry::Type<ReflectionProbe>();

        Registry::Type<SceneSettings>();
    }
}
