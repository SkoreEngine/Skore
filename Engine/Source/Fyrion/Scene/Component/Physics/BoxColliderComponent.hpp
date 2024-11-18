#pragma once
#include "Fyrion/Scene/Component/Component.hpp"

namespace Fyrion
{
    class FY_API BoxColliderComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);

        const Vec3& GetHalfSize() const;
        void        SetHalfSize(const Vec3& halfSize);
        f32         GetDensity() const;
        void        SetDensity(float density);
        bool        IsIsSensor() const;
        void        SetIsSensor(bool isSensor);

        void CollectShapes(Array<BodyShapeBuilder>& shapes) override;

        static void RegisterType(NativeTypeHandler<BoxColliderComponent>& type);

    private:
        Vec3 halfSize = {1.0, 1.0, 1.0};
        f32  density = 1000.0f;
        bool isSensor = false;
    };
}
