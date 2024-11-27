#pragma once
#include "Component.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"


namespace Skore
{
    class RenderProxy;
}

namespace Skore
{
    class TransformComponent;

    class SK_API LightComponent : public Component
    {
    public:
        SK_BASE_TYPES(Component);


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
