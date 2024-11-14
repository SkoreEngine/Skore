#pragma once
#include "Component.hpp"

namespace Fyrion
{
    class RenderService;
    class TransformComponent;

    enum class Projection : i32
    {
        Perspective = 1,
        Orthogonal  = 2
    };


    class CameraComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);

        void OnStart() override;
        void OnChange() override;
        void OnDestroy() override;

        void ProcessEvent(const SceneEventDesc& event) override;

        static void RegisterType(NativeTypeHandler<CameraComponent>& type);

    private:
        Projection projection = Projection::Perspective;
        f32        fov = 60;
        f32        near = 0.1;
        f32        far = 1000.f;
        bool       current = false;

        TransformComponent* transformComponent = nullptr;
        RenderService*      renderService = nullptr;
    };
}
