#include "EnvironmentComponent.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Graphics/Assets/TextureAsset.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Graphics/RenderProxy.hpp"

namespace Fyrion
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
