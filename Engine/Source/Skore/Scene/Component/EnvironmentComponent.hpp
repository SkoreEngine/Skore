#pragma once
#include "Component.hpp"

namespace Skore
{
    class TextureAsset;
    class RenderProxy;

    class EnvironmentComponent : public Component
    {
    public:
        SK_BASE_TYPES(Component);

        void OnStart() override;
        void OnDestroy() override;
        void OnChange() override;

        static void RegisterType(NativeTypeHandler<EnvironmentComponent>& type);

    private:
        TextureAsset* panoramaSky = nullptr;
        RenderProxy* renderProxy = nullptr;
    };
}
