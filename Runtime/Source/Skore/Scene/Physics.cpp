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
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include "Jolt/Renderer/DebugRenderer.h"

#include "Components/CharacterController.hpp"
#include "Components/RigidBody.hpp"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Skore/App.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"

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

	class JoltDebugRenderer : public JPH::DebugRenderer
	{
	public:
		static_assert(DebugPhysicsVertexSize == sizeof(Vertex), "DebugPhysicsVertexSize must match Vertex size");

		GPUCommandBuffer* cmd = nullptr;
		GPUPipeline*      pipeline = nullptr;

		struct GeometryBatch : JPH::RefTargetVirtual
		{
			GPUBuffer* vertexBuffer = nullptr;
			GPUBuffer* indexBuffer = nullptr;
			u32 indexCount = 0;

			int m_refCount = 0;

			void AddRef() override
			{
				++m_refCount;
			}

			void Release() override
			{
				--m_refCount;

				if (m_refCount == 0)
				{
					if (vertexBuffer)
					{
						vertexBuffer->Destroy();
						vertexBuffer = nullptr;
					}

					if (indexBuffer)
					{
						indexBuffer->Destroy();
						indexBuffer = nullptr;
					}

					auto* pThis = this;
					DestroyAndFree(pThis);
				}
			}
		};

		JoltDebugRenderer()
		{
			Initialize();
		}

		void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override
		{
			SK_ASSERT(false, "TODO");
		}

		void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow) override
		{
			SK_ASSERT(false, "TODO");
		}

		Batch CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override
		{
			SK_ASSERT(false, "TODO");
			return JPH::DebugRenderer::Batch();
		}

		Batch CreateTriangleBatch(const Vertex* inVertices, int inVertexCount, const JPH::uint32* inIndices, int inIndexCount) override
		{
			GeometryBatch* geometryBatch = Alloc<GeometryBatch>();
			geometryBatch->indexCount = inIndexCount;

			geometryBatch->vertexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(Vertex) * inVertexCount,
				.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDest,
			});

			Graphics::UploadBufferData(BufferUploadInfo{
				.buffer = geometryBatch->vertexBuffer,
				.data = inVertices,
				.size = sizeof(Vertex) * inVertexCount,
			});

			geometryBatch->indexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(JPH::uint32) * inIndexCount,
				.usage = ResourceUsage::IndexBuffer | ResourceUsage::CopyDest,
			});

			Graphics::UploadBufferData(BufferUploadInfo{
				.buffer = geometryBatch->indexBuffer,
				.data = inIndices,
				.size = sizeof(JPH::uint32) * inIndexCount
			});

			return geometryBatch;
		}

		void DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds, float inLODScaleSq, JPH::ColorArg inModelColor, const GeometryRef& inGeometry, ECullMode inCullMode,
		                  ECastShadow inCastShadow, EDrawMode inDrawMode) override
		{

			u32 uiLod = 0;
			if (inGeometry->mLODs.size() > 1)
			{
				uiLod = 1;
			}

			if (inGeometry->mLODs.size() > 2)
			{
				uiLod = 2;
			}

			const GeometryBatch* pBatch = static_cast<const GeometryBatch*>(inGeometry->mLODs[uiLod].mTriangleBatch.GetPtr());

			cmd->BindVertexBuffer(0, pBatch->vertexBuffer, 0);
			cmd->BindIndexBuffer(pBatch->indexBuffer, 0, IndexType::Uint32);
			cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(JPH::RMat44Arg), &inModelMatrix);
			cmd->DrawIndexed(pBatch->indexCount, 1, 0, 0, 0);
		}

		void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) override
		{
			SK_ASSERT(false, "TODO");
		}
	};

	static JoltDebugRenderer* debugRenderer = nullptr;

	struct PhysicsScene::Context
	{
		JPH::TempAllocatorImpl tempAllocator = JPH::TempAllocatorImpl(10 * 1024 * 1024);
		JPH::PhysicsSystem     physicsSystem{};
		f32                    stepSize{};
		f64                    accumulator = 0.0;

		BroadPhaseLayerInterfaceImpl      broadPhaseLayerInterfaceImpl = {};
		ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilterImpl = {};
		ObjectLayerPairFilterImpl         objectLayerPairFilterImpl = {};

		JPH::JobSystemThreadPool        jobSystem = JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);
		HashSet<JPH::CharacterVirtual*> virtualCharacters;
		Queue<Entity*> requireUpdate;
	};

	void PhysicsInit()
	{
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		debugRenderer = new JoltDebugRenderer();
	}

	void PhysicsShutdown()
	{
		delete debugRenderer;
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
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
		entity->m_physicsUpdatedFrame = App::Frame();

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
			struct ShapeData
			{
				JPH::RefConst<JPH::Shape> shape;
				JPH::Vec3Arg center;
			};

			Array<ShapeData> arrShapes{};

			for (BodyShapeBuilder& shape : collector.shapes)
			{
				SK_ASSERT(shape.bodyShape != BodyShapeType::None, "shape needs a body shape type");
				switch (shape.bodyShape)
				{
					case BodyShapeType::Box:
					{
						JPH::BoxShapeSettings boxShapeSettings(Cast(shape.size));
						boxShapeSettings.mDensity = shape.density;
						arrShapes.EmplaceBack(boxShapeSettings.Create().Get(), Cast(shape.center));
					}
					break;
				}

				if (shape.sensor)
				{
					hasSensor = true;
				}
			}

			JPH::Ref compound = new JPH::StaticCompoundShapeSettings{};
			for (auto& shapeData : arrShapes)
			{
				compound->AddShape(shapeData.center, JPH::QuatArg::sIdentity(), shapeData.shape);
			}
			JPH::RefConst compoundShape = compound->Create().Get();
			JPH::ScaledShapeSettings scaledShapeSettings(compoundShape, Cast(Math::GetScale(entity->GetGlobalTransform())));
			scaledShape = scaledShapeSettings.Create().Get();
		}

		if (entity->HasFlag(EntityFlags::HasCharacterController))
		{
			if (CharacterController* characterController = entity->GetComponent<CharacterController>())
			{
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
				return;
			}

			//no character controller found. make it a rigid body
			entity->RemoveFlag(EntityFlags::HasCharacterController);
		}

		if (!scaledShape)
		{
			//TODO remove physics flag?
			entity->m_physicsId = U64_MAX;
			return;
		}

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

	void PhysicsScene::UnregisterPhysicsEntity(Entity* entity)
	{
		if (entity->m_physicsId != U64_MAX)
		{
			if (entity->HasFlag(EntityFlags::HasCharacterController))
			{
				JPH::CharacterVirtual* characterVirtual = static_cast<JPH::CharacterVirtual*>(IntToPtr(entity->m_physicsId));
				context->virtualCharacters.Erase(characterVirtual);
				delete characterVirtual;
				entity->m_physicsId = U64_MAX;
			}
			else
			{
				JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
				JPH::BodyID         id = JPH::BodyID(entity->m_physicsId);
				bodyInterface.RemoveBody(id);
				entity->m_physicsId = U64_MAX;
			}
		}
	}

	void PhysicsScene::PhysicsEntityRequireUpdate(Entity* entity)
	{
		if (entity->HasFlag(EntityFlags::HasPhysics) && (entity->m_physicsId != U64_MAX || entity->m_started) && entity->m_physicsUpdatedFrame < App::Frame())
		{
			entity->m_physicsUpdatedFrame = App::Frame();
			context->requireUpdate.Enqueue(entity);
		}
	}

	void PhysicsScene::UpdateTransform(Entity* entity)
	{
		if (entity->HasFlag(EntityFlags::HasCharacterController))
		{
			JPH::CharacterVirtual* characterVirtual = static_cast<JPH::CharacterVirtual*>(IntToPtr(entity->m_physicsId));
			characterVirtual->SetPosition(Cast(Math::GetTranslation(entity->GetGlobalTransform())));
			characterVirtual->SetRotation(Cast(Math::GetQuaternion(entity->GetGlobalTransform())));
		}
		else
		{
			JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
			JPH::BodyID         id = JPH::BodyID(entity->m_physicsId);
			bodyInterface.SetPositionAndRotation(id, Cast(Math::GetTranslation(entity->GetGlobalTransform())), Cast(Math::GetQuaternion(entity->GetGlobalTransform())), JPH::EActivation::DontActivate);
		}
	}

	void PhysicsScene::DrawEntities(GPUCommandBuffer* cmd, GPUPipeline* pipeline, const HashSet<Entity*>& entities)
	{
		if (!context) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();

		debugRenderer->cmd = cmd;
		debugRenderer->pipeline = pipeline;

		for (Entity* entity : entities)
		{
			if (entity->m_physicsId != U64_MAX)
			{
				if (entity->HasFlag(EntityFlags::HasCharacterController))
				{
					JPH::CharacterVirtual* characterVirtual = static_cast<JPH::CharacterVirtual*>(IntToPtr(entity->m_physicsId));
					characterVirtual->GetShape()->Draw(debugRenderer, characterVirtual->GetCenterOfMassTransform(), JPH::Vec3Arg(1.0, 1.0, 1.0), {}, false, true);
				}
				else
				{
					auto id = JPH::BodyID(entity->m_physicsId);
					if (JPH::RefConst<JPH::Shape> shape = bodyInterface.GetShape(id))
					{
						shape->Draw(debugRenderer, bodyInterface.GetCenterOfMassTransform(id), JPH::Vec3Arg(1.0, 1.0, 1.0), {}, false, true);
					}
				}
			}
		}
	}

	void PhysicsScene::ExecuteEvents()
	{
		while (!context->requireUpdate.IsEmpty())
		{
			Entity* entity = context->requireUpdate.Dequeue();
			UnregisterPhysicsEntity(entity);
			RegisterPhysicsEntity(entity);
		}
	}

	void PhysicsScene::OnUpdate()
	{
		const int collisionSteps = 1;
		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();

		for (JPH::CharacterVirtual* characterVirtual : context->virtualCharacters)
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
