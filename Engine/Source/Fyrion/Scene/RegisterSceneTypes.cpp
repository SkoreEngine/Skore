#include "Scene.hpp"
#include "GameObject.hpp"
#include "Component/Component.hpp"
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
        Registry::Type<Component>();
        Registry::Type<Service>();
        Registry::Type<TransformComponent>();
        Registry::Type<RenderComponent>();

        //services
        Registry::Type<RenderService>();
    }
}
