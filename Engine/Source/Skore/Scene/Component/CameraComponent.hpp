#pragma once
#include "Component.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"

namespace Skore
{
    class RenderProxy;
    class TransformComponent;

    class CameraComponent : public Component
    {
    public:
        SK_BASE_TYPES(Component);

        void OnStart() override;
        void OnChange() override;
        void OnDestroy() override;

        void ProcessEvent(const SceneEventDesc& event) override;

        static void RegisterType(NativeTypeHandler<CameraComponent>& type);

    private:
        CameraProjection projection = CameraProjection::Perspective;
        f32              fov = 60;
        f32              near = 0.1;
        f32              far = 1000.f;
        bool             current = false;

        TransformComponent* transformComponent = nullptr;
        RenderProxy*        renderProxy = nullptr;
    };
}
