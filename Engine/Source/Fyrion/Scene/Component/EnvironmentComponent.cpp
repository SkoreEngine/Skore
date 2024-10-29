#include "EnvironmentComponent.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Graphics/Assets/TextureAsset.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Scene/Service/RenderService.hpp"

namespace Fyrion
{
    void EnvironmentComponent::OnStart()
    {
        renderService = gameObject->GetScene()->GetService<RenderService>();

        if (renderService != nullptr && panoramaSky != nullptr)
        {
            renderService->SetPanoramaSky(panoramaSky);
        }
    }

    void EnvironmentComponent::OnDestroy()
    {
        if (renderService)
        {
            renderService->SetPanoramaSky(nullptr);
        }

    }

    void EnvironmentComponent::OnChange()
    {
        if (renderService)
        {
            renderService->SetPanoramaSky(panoramaSky);
        }
    }

    void EnvironmentComponent::RegisterType(NativeTypeHandler<EnvironmentComponent>& type)
    {
        type.Field<&EnvironmentComponent::panoramaSky>("panoramaSky").Attribute<UIProperty>();
        type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false});
    }
}
