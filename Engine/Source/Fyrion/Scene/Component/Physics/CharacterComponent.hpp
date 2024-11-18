#pragma once
#include "Fyrion/Scene/Component/Component.hpp"

namespace Fyrion
{
    class FY_API CharacterComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);

        f32  GetHeight() const;
        void SetHeight(f32 height);
        f32  GetRadius() const;
        void SetRadius(f32 radius);
        f32  GetMaxSlopeAngle() const;
        void SetMaxSlopeAngle(f32 maxSlopeAngle);
        f32  GetMaxStrength() const;
        void SetMaxStrength(f32 maxStrength);
        f32  GetMass() const;
        void SetMass(f32 mass);
        f32  GetPredictiveContactDistance() const;
        void SetPredictiveContactDistance(f32 predictiveContactDistance);
        u32  GetMaxCollisionIterations() const;
        void SetMaxCollisionIterations(u32 maxCollisionIterations);
        u32  GetMaxConstraintIterations() const;
        void SetMaxConstraintIterations(u32 maxConstraintIterations);
        f32  GetMinTimeRemaining() const;
        void SetMinTimeRemaining(f32 minTimeRemaining);
        f32  GetCollisionTolerance() const;
        void SetCollisionTolerance(f32 collisionTolerance);
        f32  GetCharacterPadding() const;
        void SetCharacterPadding(f32 characterPadding);
        u32  GetMaxNumHits() const;
        void SetMaxNumHits(u32 maxNumHits);
        f32  GetHitReductionCosMaxAngle() const;
        void SetHitReductionCosMaxAngle(f32 hitReductionCosMaxAngle);
        f32  GetPenetrationRecoverySpeed() const;
        void SetPenetrationRecoverySpeed(f32 penetrationRecoverySpeed);
        Vec3 GetShapeOffset() const;
        void SetShapeOffset(const Vec3& shapeOffset);

        Vec3 GetUp() const;
        void SetUp(const Vec3& up);
        Vec3 GetLinearVelocity() const;
        void SetLinearVelocity(const Vec3& linearVelocity);
        bool IsOnGround() const;
        void SetOnGround(bool onGround);

        static void RegisterType(NativeTypeHandler<CharacterComponent>& type);

    private:
        f32  height = 1.35f;
        f32  radius = 0.3f;
        f32  maxSlopeAngle{45.f};
        f32  maxStrength = 100.0f;
        f32  mass = 70.0f;
        f32  predictiveContactDistance = 0.1f;
        u32  maxCollisionIterations = 5;
        u32  maxConstraintIterations = 15;
        f32  minTimeRemaining = 1.0e-4f;
        f32  collisionTolerance = 1.0e-3f;
        f32  characterPadding = 0.02f;
        u32  maxNumHits = 256;
        f32  hitReductionCosMaxAngle = 0.999f;
        f32  penetrationRecoverySpeed = 1.0f;
        Vec3 shapeOffset{};

        Vec3    up = Vec3::AxisY();
        Vec3    linearVelocity{};
        bool    onGround{};
    };
}
