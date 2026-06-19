#include "Skore/Scene/Components/CharacterController.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	void CharacterController::OnCreate()
	{
		entity->AddFlag(EntityFlags::HasPhysics);
		entity->AddFlag(EntityFlags::HasCharacterController);
		PhysicsRequireUpdate();
	}

	void CharacterController::OnDestroy()
	{
		PhysicsRequireUpdate();
	}

	void CharacterController::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::DrawPhysicsShape)
		{
			DrawPhysicsShapeEvent* data = static_cast<DrawPhysicsShapeEvent*>(event.eventData);
			scene->physicsScene.DrawShape(entity, data->cmd, data->pipeline);
		}
	}

	f32 CharacterController::GetHeight() const
	{
		return height;
	}

	void CharacterController::SetHeight(f32 height)
	{
		this->height = height;
	}

	f32 CharacterController::GetRadius() const
	{
		return radius;
	}

	void CharacterController::SetRadius(f32 radius)
	{
		this->radius = radius;
	}

	f32 CharacterController::GetMaxSlopeAngle() const
	{
		return maxSlopeAngle;
	}

	void CharacterController::SetMaxSlopeAngle(f32 maxSlopeAngle)
	{
		this->maxSlopeAngle = maxSlopeAngle;
	}

	f32 CharacterController::GetMaxStrength() const
	{
		return maxStrength;
	}

	void CharacterController::SetMaxStrength(f32 maxStrength)
	{
		this->maxStrength = maxStrength;
	}

	f32 CharacterController::GetMass() const
	{
		return mass;
	}

	void CharacterController::SetMass(f32 mass)
	{
		this->mass = mass;
	}

	f32 CharacterController::GetPredictiveContactDistance() const
	{
		return predictiveContactDistance;
	}

	void CharacterController::SetPredictiveContactDistance(f32 predictiveContactDistance)
	{
		this->predictiveContactDistance = predictiveContactDistance;
	}

	u32 CharacterController::GetMaxCollisionIterations() const
	{
		return maxCollisionIterations;
	}

	void CharacterController::SetMaxCollisionIterations(u32 maxCollisionIterations)
	{
		this->maxCollisionIterations = maxCollisionIterations;
	}

	u32 CharacterController::GetMaxConstraintIterations() const
	{
		return maxConstraintIterations;
	}

	void CharacterController::SetMaxConstraintIterations(u32 maxConstraintIterations)
	{
		this->maxConstraintIterations = maxConstraintIterations;
	}

	f32 CharacterController::GetMinTimeRemaining() const
	{
		return minTimeRemaining;
	}

	void CharacterController::SetMinTimeRemaining(f32 minTimeRemaining)
	{
		this->minTimeRemaining = minTimeRemaining;
	}

	f32 CharacterController::GetCollisionTolerance() const
	{
		return collisionTolerance;
	}

	void CharacterController::SetCollisionTolerance(f32 collisionTolerance)
	{
		this->collisionTolerance = collisionTolerance;
	}

	f32 CharacterController::GetCharacterPadding() const
	{
		return characterPadding;
	}

	void CharacterController::SetCharacterPadding(f32 characterPadding)
	{
		this->characterPadding = characterPadding;
	}

	u32 CharacterController::GetMaxNumHits() const
	{
		return maxNumHits;
	}

	void CharacterController::SetMaxNumHits(u32 maxNumHits)
	{
		this->maxNumHits = maxNumHits;
	}

	f32 CharacterController::GetHitReductionCosMaxAngle() const
	{
		return hitReductionCosMaxAngle;
	}

	void CharacterController::SetHitReductionCosMaxAngle(f32 hitReductionCosMaxAngle)
	{
		this->hitReductionCosMaxAngle = hitReductionCosMaxAngle;
	}

	f32 CharacterController::GetPenetrationRecoverySpeed() const
	{
		return penetrationRecoverySpeed;
	}

	void CharacterController::SetPenetrationRecoverySpeed(f32 penetrationRecoverySpeed)
	{
		this->penetrationRecoverySpeed = penetrationRecoverySpeed;
	}

	Vec3 CharacterController::GetShapeOffset() const
	{
		return shapeOffset;
	}

	void CharacterController::SetShapeOffset(const Vec3& shapeOffset)
	{
		this->shapeOffset = shapeOffset;
	}

	Vec3 CharacterController::GetUp() const
	{
		return up;
	}

	void CharacterController::SetUp(const Vec3& up)
	{
		this->up = up;
	}

	Vec3 CharacterController::GetLinearVelocity() const
	{
		return linearVelocity;
	}

	void CharacterController::SetLinearVelocity(const Vec3& linearVelocity)
	{
		this->linearVelocity = linearVelocity;
	}

	bool CharacterController::IsOnGround() const
	{
		return onGround;
	}

	void CharacterController::SetOnGround(bool onGround)
	{
		this->onGround = onGround;
	}

	void CharacterController::RegisterType(NativeReflectType<CharacterController>& type)
	{
		type.Field<&CharacterController::height>("height");
		type.Field<&CharacterController::radius>("radius");
		type.Field<&CharacterController::maxSlopeAngle>("maxSlopeAngle");
		type.Field<&CharacterController::maxStrength>("maxStrength");
		type.Field<&CharacterController::mass>("mass");
		type.Field<&CharacterController::predictiveContactDistance>("predictiveContactDistance");
		type.Field<&CharacterController::maxCollisionIterations>("maxCollisionIterations");
		type.Field<&CharacterController::maxConstraintIterations>("maxConstraintIterations");
		type.Field<&CharacterController::minTimeRemaining>("minTimeRemaining");
		type.Field<&CharacterController::collisionTolerance>("collisionTolerance");
		type.Field<&CharacterController::characterPadding>("characterPadding");
		type.Field<&CharacterController::maxNumHits>("maxNumHits");
		type.Field<&CharacterController::hitReductionCosMaxAngle>("hitReductionCosMaxAngle");
		type.Field<&CharacterController::penetrationRecoverySpeed>("penetrationRecoverySpeed");
		type.Field<&CharacterController::shapeOffset>("shapeOffset");

		type.Function<&CharacterController::GetUp>("GetUp");
		type.Function<&CharacterController::SetUp>("SetUp", "up");
		type.Function<&CharacterController::GetLinearVelocity>("GetLinearVelocity");
		type.Function<&CharacterController::SetLinearVelocity>("SetLinearVelocity", "linearVelocity");
		type.Function<&CharacterController::IsOnGround>("IsOnGround");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}
}