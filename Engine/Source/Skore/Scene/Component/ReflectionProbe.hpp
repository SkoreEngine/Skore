#pragma once
#include "Component.hpp"
#include "Skore/Graphics/RenderUtils.hpp"


namespace Skore
{
    class SK_API ReflectionProbe : public Component
    {
    public:
        SK_BASE_TYPES(Component);

        void OnStart() override;
        void OnDestroy() override;

        static void RegisterType(NativeTypeHandler<ReflectionProbe>& type);

        void Bake();
    private:
        SpecularMapGenerator specularMapGenerator;

        u16 mips = 4;
        u32 size = 128;
    };
}
