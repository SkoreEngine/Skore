#include "CharacterComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"


namespace Skore
{
    f32 CharacterComponent::GetHeight() const
    {
        return height;
    }

    void CharacterComponent::SetHeight(f32 height)
    {
        this->height = height;
    }

    f32 CharacterComponent::GetRadius() const
    {
        return radius;
    }

    void CharacterComponent::SetRadius(f32 radius)
    {
        this->radius = radius;
    }

    f32 CharacterComponent::GetMaxSlopeAngle() const
    {
        return maxSlopeAngle;
    }

    void CharacterComponent::SetMaxSlopeAngle(f32 maxSlopeAngle)
    {
        this->maxSlopeAngle = maxSlopeAngle;
    }

    f32 CharacterComponent::GetMaxStrength() const
    {
        return maxStrength;
    }

    void CharacterComponent::SetMaxStrength(f32 maxStrength)
    {
        this->maxStrength = maxStrength;
    }

    f32 CharacterComponent::GetMass() const
    {
        return mass;
    }

    void CharacterComponent::SetMass(f32 mass)
    {
        this->mass = mass;
    }

    f32 CharacterComponent::GetPredictiveContactDistance() const
    {
        return predictiveContactDistance;
    }

    void CharacterComponent::SetPredictiveContactDistance(f32 predictiveContactDistance)
    {
        this->predictiveContactDistance = predictiveContactDistance;
    }

    u32 CharacterComponent::GetMaxCollisionIterations() const
    {
        return maxCollisionIterations;
    }

    void CharacterComponent::SetMaxCollisionIterations(u32 maxCollisionIterations)
    {
        this->maxCollisionIterations = maxCollisionIterations;
    }

    u32 CharacterComponent::GetMaxConstraintIterations() const
    {
        return maxConstraintIterations;
    }

    void CharacterComponent::SetMaxConstraintIterations(u32 maxConstraintIterations)
    {
        this->maxConstraintIterations = maxConstraintIterations;
    }

    f32 CharacterComponent::GetMinTimeRemaining() const
    {
        return minTimeRemaining;
    }

    void CharacterComponent::SetMinTimeRemaining(f32 minTimeRemaining)
    {
        this->minTimeRemaining = minTimeRemaining;
    }

    f32 CharacterComponent::GetCollisionTolerance() const
    {
        return collisionTolerance;
    }

    void CharacterComponent::SetCollisionTolerance(f32 collisionTolerance)
    {
        this->collisionTolerance = collisionTolerance;
    }

    f32 CharacterComponent::GetCharacterPadding() const
    {
        return characterPadding;
    }

    void CharacterComponent::SetCharacterPadding(f32 characterPadding)
    {
        this->characterPadding = characterPadding;
    }

    u32 CharacterComponent::GetMaxNumHits() const
    {
        return maxNumHits;
    }

    void CharacterComponent::SetMaxNumHits(u32 maxNumHits)
    {
        this->maxNumHits = maxNumHits;
    }

    f32 CharacterComponent::GetHitReductionCosMaxAngle() const
    {
        return hitReductionCosMaxAngle;
    }

    void CharacterComponent::SetHitReductionCosMaxAngle(f32 hitReductionCosMaxAngle)
    {
        this->hitReductionCosMaxAngle = hitReductionCosMaxAngle;
    }

    f32 CharacterComponent::GetPenetrationRecoverySpeed() const
    {
        return penetrationRecoverySpeed;
    }

    void CharacterComponent::SetPenetrationRecoverySpeed(f32 penetrationRecoverySpeed)
    {
        this->penetrationRecoverySpeed = penetrationRecoverySpeed;
    }

    Vec3 CharacterComponent::GetShapeOffset() const
    {
        return shapeOffset;
    }

    void CharacterComponent::SetShapeOffset(const Vec3& shapeOffset)
    {
        this->shapeOffset = shapeOffset;
    }

    Vec3 CharacterComponent::GetUp() const
    {
        return up;
    }

    void CharacterComponent::SetUp(const Vec3& up)
    {
        this->up = up;
    }

    Vec3 CharacterComponent::GetLinearVelocity() const
    {
        return linearVelocity;
    }

    void CharacterComponent::SetLinearVelocity(const Vec3& linearVelocity)
    {
        this->linearVelocity = linearVelocity;
    }

    bool CharacterComponent::IsOnGround() const
    {
        return onGround;
    }

    void CharacterComponent::SetOnGround(bool onGround)
    {
        this->onGround = onGround;
    }

    void CharacterComponent::RegisterType(NativeTypeHandler<CharacterComponent>& type)
    {
        type.Field<&CharacterComponent::height>("height").Attribute<UIProperty>();
        type.Field<&CharacterComponent::radius>("radius").Attribute<UIProperty>();
        type.Field<&CharacterComponent::maxSlopeAngle>("maxSlopeAngle").Attribute<UIProperty>();
        type.Field<&CharacterComponent::maxStrength>("maxStrength").Attribute<UIProperty>();
        type.Field<&CharacterComponent::mass>("mass").Attribute<UIProperty>();
        type.Field<&CharacterComponent::predictiveContactDistance>("predictiveContactDistance").Attribute<UIProperty>();
        type.Field<&CharacterComponent::maxCollisionIterations>("maxCollisionIterations").Attribute<UIProperty>();
        type.Field<&CharacterComponent::maxConstraintIterations>("maxConstraintIterations").Attribute<UIProperty>();
        type.Field<&CharacterComponent::minTimeRemaining>("minTimeRemaining").Attribute<UIProperty>();
        type.Field<&CharacterComponent::collisionTolerance>("collisionTolerance").Attribute<UIProperty>();
        type.Field<&CharacterComponent::characterPadding>("characterPadding").Attribute<UIProperty>();
        type.Field<&CharacterComponent::maxNumHits>("maxNumHits").Attribute<UIProperty>();
        type.Field<&CharacterComponent::hitReductionCosMaxAngle>("hitReductionCosMaxAngle").Attribute<UIProperty>();
        type.Field<&CharacterComponent::penetrationRecoverySpeed>("penetrationRecoverySpeed").Attribute<UIProperty>();
        type.Field<&CharacterComponent::shapeOffset>("shapeOffset").Attribute<UIProperty>();

        type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
    }
}
