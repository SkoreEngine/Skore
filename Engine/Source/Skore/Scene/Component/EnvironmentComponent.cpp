#include "EnvironmentComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"
#include "Skore/Scene/GameObject.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/RenderProxy.hpp"

namespace Skore
{
    void EnvironmentComponent::OnStart()
    {
        renderProxy = gameObject->GetScene()->GetProxy<RenderProxy>();

        if (renderProxy != nullptr && panoramaSky != nullptr)
        {
            renderProxy->SetPanoramaSky(panoramaSky);
        }
    }

    void EnvironmentComponent::OnDestroy()
    {
        if (renderProxy)
        {
            renderProxy->SetPanoramaSky(nullptr);
        }
    }

    void EnvironmentComponent::OnChange()
    {
        if (renderProxy)
        {
            renderProxy->SetPanoramaSky(panoramaSky);
        }
    }

    void EnvironmentComponent::RegisterType(NativeTypeHandler<EnvironmentComponent>& type)
    {
        type.Field<&EnvironmentComponent::panoramaSky>("panoramaSky").Attribute<UIProperty>();
        type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false});
    }
}
