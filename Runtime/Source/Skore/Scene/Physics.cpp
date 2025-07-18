// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Physics.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include "Components/CharacterController.hpp"
#include "Components/RigidBody.hpp"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Skore/App.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::Physics");

	namespace PhysicsLayers
	{
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING = 1;
		static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
	}

	class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			switch (inObject1)
			{
				case PhysicsLayers::NON_MOVING:
					return inObject2 == PhysicsLayers::MOVING;
				case PhysicsLayers::MOVING:
					return true;
				default:
					SK_ASSERT(false, "Error on physics");
					return false;
			}
		}
	};

	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr u32                  NUM_LAYERS(2);
	}


	class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		BroadPhaseLayerInterfaceImpl()
		{
			objectToBroadPhase[PhysicsLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
			objectToBroadPhase[PhysicsLayers::MOVING] = BroadPhaseLayers::MOVING;
		}

		u32 GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
		{
			SK_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS, "Error on Physics");
			return objectToBroadPhase[inLayer];
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
                    return "NON_MOVING";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
                    return "MOVING";
                default:
                    SK_ASSERT(false, "Error on Physics");
                    return "INVALID";
            }
        }
#endif

	private:
		JPH::BroadPhaseLayer objectToBroadPhase[PhysicsLayers::NUM_LAYERS];
	};

	class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			switch (inLayer1)
			{
				case PhysicsLayers::NON_MOVING:
					return inLayer2 == BroadPhaseLayers::MOVING;
				case PhysicsLayers::MOVING:
					return true;
				default:
					SK_ASSERT(false, "Error on Physics");
					return false;
			}
		}
	};

	SK_FINLINE JPH::Vec3 Cast(const Vec3& vec3)
	{
		return JPH::Vec3{vec3.x, vec3.y, vec3.z};
	}

	SK_FINLINE Vec3 Cast(const JPH::Vec3& vec3)
	{
		return Vec3{vec3.GetX(), vec3.GetY(), vec3.GetZ()};
	}

	SK_FINLINE JPH::Vec4 Cast(const Vec4& vec4)
	{
		return JPH::Vec4{vec4.x, vec4.y, vec4.z, vec4.w};
	}

	SK_FINLINE JPH::Quat Cast(const Quat& quat)
	{
		return JPH::Quat{quat.x, quat.y, quat.z, quat.w};
	}

	SK_FINLINE Quat Cast(const JPH::Quat& quat)
	{
		return Quat{quat.GetX(), quat.GetY(), quat.GetZ(), quat.GetW()};
	}

	SK_FINLINE JPH::EMotionQuality CastQuality(const CollisionDetectionType& collisionDetection)
	{
		switch (collisionDetection)
		{
			case CollisionDetectionType::Discrete: return JPH::EMotionQuality::Discrete;
			case CollisionDetectionType::LinearCast: return JPH::EMotionQuality::LinearCast;
		}
		return JPH::EMotionQuality::Discrete;
	}

	struct PhysicsScene::Context
	{
		JPH::TempAllocatorImpl tempAllocator = JPH::TempAllocatorImpl(10 * 1024 * 1024);
		JPH::PhysicsSystem     physicsSystem{};
		f32                    stepSize{};
		f64                    accumulator = 0.0;

		BroadPhaseLayerInterfaceImpl      broadPhaseLayerInterfaceImpl = {};
		ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilterImpl = {};
		ObjectLayerPairFilterImpl         objectLayerPairFilterImpl = {};
		JPH::JobSystemThreadPool          jobSystem = JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);
		HashSet<JPH::CharacterVirtual*>   virtualCharacters;
	};

	void PhysicsInit()
	{
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
	}

	PhysicsScene::PhysicsScene()
	{
		context = Alloc<Context>();

		//TODO - get from settings.
		u32 maxBodies = 65536;
		u32 maxBodyPairs = 65536;
		u32 maxContactConstraints = 10240;
		u32 physicsTicksPerSeconds = 75;

		context->stepSize = 1.0f / (f32)physicsTicksPerSeconds;

		context->physicsSystem.Init(
			maxBodies,
			0,
			maxBodyPairs,
			maxContactConstraints,
			context->broadPhaseLayerInterfaceImpl,
			context->objectVsBroadPhaseLayerFilterImpl,
			context->objectLayerPairFilterImpl);
	}

	PhysicsScene::~PhysicsScene()
	{
		DestroyAndFree(context);
	}

	void PhysicsScene::RegisterPhysicsEntity(Entity* entity)
	{
		static ShapeCollector collector;
		collector.shapes.Clear();

		EntityEventDesc eventDesc = {};
		eventDesc.type = EntityEventType::CollectPhysicsShapes;
		eventDesc.eventData = &collector;
		entity->NotifyEvent(eventDesc, false);

		bool                      hasSensor = false;
		JPH::RefConst<JPH::Shape> scaledShape = {};

		if (!collector.shapes.Empty())
		{
			Array<JPH::RefConst<JPH::Shape>> arrShapes{};

			for (BodyShapeBuilder& shape : collector.shapes)
			{
				SK_ASSERT(shape.bodyShape != BodyShapeType::None, "shape needs a body shape type");
				switch (shape.bodyShape)
				{
					case BodyShapeType::Box:
					{
						JPH::BoxShapeSettings boxShapeSettings(Cast(shape.size));
						boxShapeSettings.mDensity = shape.density;
						arrShapes.EmplaceBack(boxShapeSettings.Create().Get());
					}
					break;
				}

				if (shape.sensor)
				{
					hasSensor = true;
				}
			}

			JPH::RefConst<JPH::Shape> finalShape;
			if (arrShapes.Size() > 1)
			{
				JPH::Ref compound = new JPH::StaticCompoundShapeSettings{};
				for (auto& shape : arrShapes)
				{
					compound->AddShape(JPH::Vec3Arg::sZero(), JPH::QuatArg::sIdentity(), shape);
				}
				finalShape = compound->Create().Get();
			}
			else
			{
				finalShape = arrShapes[0];
			}

			JPH::ScaledShapeSettings scaledShapeSettings(finalShape, Cast(Math::GetScale(entity->GetGlobalTransform())));
			scaledShape = scaledShapeSettings.Create().Get();
		}

		if (entity->HasFlag(EntityFlags::HasCharacterController))
		{
			CharacterController* characterController = entity->GetComponent<CharacterController>();
			if (!characterController)
			{
				logger.Error("Entity has a CharacterController flag but no CharacterController component");
				return;
			}

			if (!scaledShape)
			{
				scaledShape = JPH::RotatedTranslatedShapeSettings(
					JPH::Vec3(0, 0.5f * characterController->GetHeight() + characterController->GetRadius(), 0),
					JPH::Quat::sIdentity(),
					new JPH::CapsuleShape(0.5f * characterController->GetHeight(), characterController->GetRadius())).Create().Get();
			}

			JPH::CharacterVirtualSettings settings{};
			settings.mShape = scaledShape;
			settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -characterController->GetRadius());

			JPH::CharacterVirtual* characterVirtual = new JPH::CharacterVirtual(&settings,
			                                                                    Cast(Math::GetTranslation(entity->GetGlobalTransform())),
			                                                                    Cast(Math::GetQuaternion(entity->GetGlobalTransform())),
			                                                                    PtrToInt(characterController),
			                                                                    &context->physicsSystem);
			context->virtualCharacters.Insert(characterVirtual);
			entity->m_physicsId = PtrToInt(characterVirtual);
		}
		else
		{
			JPH::BodyCreationSettings bodyCreationSettings{};
			bodyCreationSettings.SetShape(scaledShape);
			bodyCreationSettings.mPosition = Cast(Math::GetTranslation(entity->GetGlobalTransform()));
			bodyCreationSettings.mRotation = Cast(Math::GetQuaternion(entity->GetGlobalTransform()));
			bodyCreationSettings.mUserData = PtrToInt(entity);
			bodyCreationSettings.mIsSensor = hasSensor;

			if (RigidBody* rigidBodyComponent = entity->GetComponent<RigidBody>())
			{
				bodyCreationSettings.mAllowDynamicOrKinematic = false;
				bodyCreationSettings.mMotionType = !rigidBodyComponent->IsKinematic() ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic;
				bodyCreationSettings.mObjectLayer = PhysicsLayers::MOVING;
				bodyCreationSettings.mAllowedDOFs = JPH::EAllowedDOFs::All;
				bodyCreationSettings.mUseManifoldReduction = true;
				bodyCreationSettings.mMotionQuality = CastQuality(rigidBodyComponent->GetCollisionDetectionType());
				bodyCreationSettings.mAllowSleeping = true;
				bodyCreationSettings.mFriction = rigidBodyComponent->GetFriction();
				bodyCreationSettings.mRestitution = rigidBodyComponent->GetRestitution();
				bodyCreationSettings.mGravityFactor = rigidBodyComponent->GetGravityFactor();
				bodyCreationSettings.mMassPropertiesOverride.mMass = rigidBodyComponent->GetMass();
				bodyCreationSettings.mLinearVelocity = Cast(rigidBodyComponent->GetLinearVelocity());
				bodyCreationSettings.mAngularVelocity = Cast(rigidBodyComponent->GetAngularVelocity());
			}
			else
			{
				bodyCreationSettings.mMotionType = JPH::EMotionType::Static;
				bodyCreationSettings.mObjectLayer = PhysicsLayers::NON_MOVING;
			}

			JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
			JPH::BodyID         id = bodyInterface.CreateAndAddBody(bodyCreationSettings, JPH::EActivation::Activate);

			entity->m_physicsId = id.GetIndexAndSequenceNumber();
		}
	}

	void PhysicsScene::OnUpdate()
	{
		const int collisionSteps = 1;

		for(JPH::CharacterVirtual* characterVirtual: context->virtualCharacters)
		{
			CharacterController* characterController = static_cast<CharacterController*>(IntToPtr(characterVirtual->GetUserData()));

			characterVirtual->SetUp(Cast(characterController->GetUp()));
			characterVirtual->SetLinearVelocity(Cast(characterController->GetLinearVelocity()));

			characterVirtual->UpdateGroundVelocity();

			JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings{};
			updateSettings.mWalkStairsMinStepForward *= 4.0f;

			characterVirtual->ExtendedUpdate(
				context->stepSize,
				-characterVirtual->GetUp() * context->physicsSystem.GetGravity().Length(),
				updateSettings,
				context->physicsSystem.GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
				context->physicsSystem.GetDefaultLayerFilter(PhysicsLayers::MOVING),
				{},
				{},
				context->tempAllocator);

			//Mat4 math = Math::Inverse(parentTransform.value) * Math::Translate(Mat4{1.0}, Cast(position)) * Math::ToMatrix4(Cast(rotation)) * Math::Scale(Mat4{1.0}, localTransform.scale);
			//TODO parent transform

			characterController->entity->SetTransform(Cast(characterVirtual->GetPosition()),
			                                          Cast(characterVirtual->GetRotation()),
			                                          characterController->entity->GetScale());

			characterController->SetOnGround(characterVirtual->IsSupported());
		}

		context->accumulator += App::DeltaTime();
		while (context->accumulator >= context->stepSize)
		{
			context->physicsSystem.Update(
				context->stepSize,
				collisionSteps,
				&context->tempAllocator,
				&context->jobSystem);
			context->accumulator -= context->stepSize;
		}

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyIDVector   outBodyIDs{};
		context->physicsSystem.GetActiveBodies(JPH::EBodyType::RigidBody, outBodyIDs);

		for (const JPH::BodyID bodyId : outBodyIDs)
		{
			JPH::Vec3 position{};
			JPH::Quat rotation{};
			bodyInterface.GetPositionAndRotation(bodyId, position, rotation);

			Entity* entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(bodyId));
			entity->SetTransform(Cast(position), Cast(rotation), entity->GetScale());

			if (RigidBody* rigidBodyComponent = entity->GetComponent<RigidBody>())
			{
				rigidBodyComponent->m_linearVelocity = Cast(bodyInterface.GetLinearVelocity(bodyId));
				rigidBodyComponent->m_angularVelocity = Cast(bodyInterface.GetAngularVelocity(bodyId));
			}
		}
	}

	void PhysicsScene::OnSceneActivated() {}

	void PhysicsScene::OnSceneDeactivated() {}
}
