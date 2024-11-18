#pragma once
#include "Component.hpp"
#include "Fyrion/Graphics/GraphicsTypes.hpp"


namespace Fyrion
{
    class RenderProxy;
}

namespace Fyrion
{
    class TransformComponent;

    class FY_API LightComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);


        LightType GetType() const;
        void      SetType(LightType type);
        f32       GetIntensity() const;
        void      SetIntensity(f32 intensity);
        f32       GetIndirectMultiplier() const;
        void      SetIndirectMultiplier(f32 indirectMultiplier);
        bool      IsCastShadows() const;
        void      SetCastShadows(bool castShadows);


        void ProcessEvent(const SceneEventDesc& event) override;
        void OnChange() override;
        void OnStart() override;
        void OnDestroy() override;


        static void RegisterType(NativeTypeHandler<LightComponent>& type);

    private:
        LightType type = LightType::Directional;
        Color     color = Color::WHITE;
        f32       intensity = 2.0;
        f32       indirectMultiplier = 1.0;
        f32       range = 10;
        bool      castShadows = false;

        TransformComponent* transformComponent = nullptr;
        RenderProxy*        renderProxy = nullptr;
    };
}
