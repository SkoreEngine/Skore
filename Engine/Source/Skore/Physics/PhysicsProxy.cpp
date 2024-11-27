#include "PhysicsProxy.hpp"

#include "Skore/Engine.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Scene/GameObject.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Component/Component.hpp"
#include "Skore/Scene/Component/TransformComponent.hpp"
#include "Skore/Scene/Component/Physics/CharacterComponent.hpp"
#include "Skore/Scene/Component/Physics/RigidBodyComponent.hpp"
#include "Jolt/Jolt.hpp"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/ScaledShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"


namespace Skore
{
    void PhysicsProxy::OnStart()
    {
        //TODO - load physics settings
        PhysicsSettings physicsSettings{};

        context = Alloc<PhysicsContext>();
        context->stepSize = 1.0f / (f32)physicsSettings.physicsTicksPerSeconds;

        context->physicsSystem.Init(
            physicsSettings.maxBodies,
            0,
            physicsSettings.maxBodyPairs,
            physicsSettings.maxContactConstraints,
            context->broadPhaseLayerInterfaceImpl,
            context->objectVsBroadPhaseLayerFilterImpl,
            context->objectLayerPairFilterImpl);
    }

    void PhysicsProxy::OnUpdate()
    {
        const int collisionSteps = 1;

        if (simulationEnabled && context)
        {
            for(JPH::CharacterVirtual* characterVirtual: context->virtualCharacters)
            {
                CharacterComponent* characterComponent = reinterpret_cast<CharacterComponent*>(characterVirtual->GetUserData());

                characterVirtual->SetUp(Cast(characterComponent->GetUp()));
                characterVirtual->SetLinearVelocity(Cast(characterComponent->GetLinearVelocity()));

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
                if (TransformComponent* transformComponent = characterComponent->gameObject->GetComponent<TransformComponent>())
                {
                    transformComponent->SetTransform(Cast(characterVirtual->GetPosition()),
                                                     Cast(characterVirtual->GetRotation()),
                                                     transformComponent->GetScale());
                }

                characterComponent->SetOnGround(characterVirtual->IsSupported());
            }

            JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();

            accumulator += Engine::DeltaTime();
            while (accumulator >= context->stepSize)
            {
                context->physicsSystem.Update(
                    context->stepSize,
                    collisionSteps,
                    &context->tempAllocator,
                    &context->jobSystem);
                accumulator -= context->stepSize;
            }

            JPH::BodyIDVector outBodyIDs{};
            context->physicsSystem.GetActiveBodies(JPH::EBodyType::RigidBody, outBodyIDs);

            for (const JPH::BodyID bodyId : outBodyIDs)
            {
                JPH::Vec3 position{};
                JPH::Quat rotation{};
                bodyInterface.GetPositionAndRotation(bodyId, position, rotation);

                GameObject* gameObject = reinterpret_cast<GameObject*>(bodyInterface.GetUserData(bodyId));

                //TODO parent transform
                if (TransformComponent* transformComponent = gameObject->GetComponent<TransformComponent>())
                {
                    transformComponent->SetTransform(Cast(position), Cast(rotation), transformComponent->GetScale());
                }

                if (RigidBodyComponent* rigidBodyComponent = gameObject->GetComponent<RigidBodyComponent>())
                {
                    rigidBodyComponent->linearVelocity = Cast(bodyInterface.GetLinearVelocity(bodyId));
                    rigidBodyComponent->angularVelocity = Cast(bodyInterface.GetAngularVelocity(bodyId));
                }
            }
        }
    }

    void PhysicsProxy::OnDestroy()
    {
        if (context)
        {
            DestroyAndFree(context);
        }
    }

    void PhysicsProxy::OnGameObjectStarted(GameObject* gameObject)
    {
        if (!context)
        {
            return;
        }

        TransformComponent* transformComponent = gameObject->GetComponent<TransformComponent>();
        if (!transformComponent)
        {
            return;
        }

        //TODO need to integrate with shapes, if no shape is added, create a default capsule?
        if (CharacterComponent* characterComponent = gameObject->GetComponent<CharacterComponent>())
        {
            JPH::RefConst shape = JPH::RotatedTranslatedShapeSettings(
                JPH::Vec3(0, 0.5f * characterComponent->GetHeight() + characterComponent->GetRadius(), 0),
                JPH::Quat::sIdentity(),
                new JPH::CapsuleShape(0.5f * characterComponent->GetHeight(), characterComponent->GetRadius())).Create().Get();

            JPH::CharacterVirtualSettings settings{};
            settings.mShape = shape;
            settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -characterComponent->GetRadius());

            JPH::CharacterVirtual* characterVirtual = new JPH::CharacterVirtual(&settings,
                                                                                Cast(Math::GetTranslation(transformComponent->GetWorldTransform())),
                                                                                Cast(Math::GetQuaternion(transformComponent->GetWorldTransform())),
                                                                                reinterpret_cast<u64>(characterComponent),
                                                                                &context->physicsSystem);

            gameObject->SetPhysicsRef(reinterpret_cast<u64>(characterVirtual));

            context->virtualCharacters.insert(characterVirtual);

            return;
        }

        Array<BodyShapeBuilder> shapes;

        for (Component* component : gameObject->GetComponents())
        {
            component->CollectShapes(shapes);
        }

        if (!shapes.Empty())
        {
            Array<JPH::RefConst<JPH::Shape>> arrShapes{};
            bool                             isSensor = false;

            for (BodyShapeBuilder& shape : shapes)
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
                    isSensor = true;
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

            JPH::ScaledShapeSettings scaledShapeSettings(finalShape, Cast(Math::GetScale(transformComponent->GetWorldTransform())));

            JPH::BodyCreationSettings bodyCreationSettings{};
            bodyCreationSettings.SetShape(scaledShapeSettings.Create().Get());
            bodyCreationSettings.mPosition = Cast(Math::GetTranslation(transformComponent->GetWorldTransform()));
            bodyCreationSettings.mRotation = Cast(Math::GetQuaternion(transformComponent->GetWorldTransform()));
            bodyCreationSettings.mUserData = reinterpret_cast<u64>(gameObject);
            bodyCreationSettings.mIsSensor = isSensor;

            if (RigidBodyComponent* rigidBodyComponent = gameObject->GetComponent<RigidBodyComponent>())
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

            gameObject->SetPhysicsRef(id.GetIndexAndSequenceNumber());
        }
    }

    void PhysicsProxy::OnGameObjectDestroyed(GameObject* gameObject)
    {
        if (!gameObject->GetScene()->IsDestroyed() && gameObject->GetPhysicsRef() != U64_MAX)
        {
            JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
            JPH::BodyID         bodyId(gameObject->GetPhysicsRef());
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);
        }
    }

    void PhysicsProxy::SetLinearAndAngularVelocity(GameObject* gameObject, const Vec3& linearVelocity, const Vec3& angularVelocity)
    {
        if (gameObject->GetPhysicsRef() != U64_MAX)
        {
            JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
            bodyInterface.SetLinearAndAngularVelocity(JPH::BodyID(gameObject->GetPhysicsRef()), Cast(linearVelocity), Cast(angularVelocity));
        }
    }

    void PhysicsProxy::EnableSimulation()
    {
        simulationEnabled = true;
    }

    void PhysicsProxy::DisableSimulation()
    {
        simulationEnabled = false;
    }
}
