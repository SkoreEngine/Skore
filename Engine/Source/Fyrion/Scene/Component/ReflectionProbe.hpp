#pragma once
#include "Component.hpp"


namespace Fyrion
{
    class FY_API ReflectionProbe : public Component
    {
    public:
        FY_BASE_TYPES(Component);

        static void RegisterType(NativeTypeHandler<ReflectionProbe>& type);

        void Bake();
    private:

    };
}