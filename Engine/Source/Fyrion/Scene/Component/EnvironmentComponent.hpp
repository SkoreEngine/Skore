#pragma once
#include "Component.hpp"

namespace Fyrion {
    class RenderService;
}

namespace Fyrion
{
    class TextureAsset;

    class EnvironmentComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);

        void OnStart() override;
        void OnDestroy() override;
        void OnChange() override;

        static void RegisterType(NativeTypeHandler<EnvironmentComponent>& type);
    private:
        TextureAsset* panoramaSky = nullptr;

        RenderService* renderService = nullptr;
    };
}
