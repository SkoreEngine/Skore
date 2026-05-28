#include "Skore/Scene/Physics.hpp"

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
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include "Jolt/Renderer/DebugRenderer.h"

#include <concurrentqueue.h>
#include <mutex>

#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Components/CharacterController.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Components/RigidBody.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Skore/App.hpp"
#include "Skore/Events.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"

namespace Skore
{
	static Logger&       logger = Logger::GetLogger("Skore::Physics");
	static u32           maxBodies = 65536;
	static u32           maxBodyPairs = 65536;
	static u32           maxContactConstraints = 10240;
	static u32           physicsTicksPerSeconds = 75;
	static PhysicsScene* currentPhysicsScene = nullptr;
	static u64           collisionMatrix[MaxLayers];


	// Object layers encode user layer (0-63) + motion type into 128 layers:
	// objectLayer = userLayer * 2 + isMoving
	namespace PhysicsLayers
	{
		static constexpr JPH::ObjectLayer NUM_LAYERS = 128;

		SK_FINLINE JPH::ObjectLayer Encode(u8 userLayer, bool isMoving)
		{
			return static_cast<JPH::ObjectLayer>(userLayer * 2 + (isMoving ? 1 : 0));
		}

		SK_FINLINE u8 GetUserLayer(JPH::ObjectLayer layer)
		{
			return static_cast<u8>(layer / 2);
		}

		SK_FINLINE bool IsMoving(JPH::ObjectLayer layer)
		{
			return (layer & 1) != 0;
		}
	}

	class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			// Static vs static never collide
			bool moving1 = PhysicsLayers::IsMoving(inObject1);
			bool moving2 = PhysicsLayers::IsMoving(inObject2);
			if (!moving1 && !moving2) return false;
			return true;
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
		u32 GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
		{
			SK_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS, "Error on Physics");
			return PhysicsLayers::IsMoving(inLayer) ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
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
	};

	class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			if (!PhysicsLayers::IsMoving(inLayer1))
			{
				// Non-moving only collides with MOVING broad phase
				return inLayer2 == BroadPhaseLayers::MOVING;
			}
			// Moving collides with both
			return true;
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

	class LayerMaskBodyFilter : public JPH::BodyFilter
	{
	public:
		explicit LayerMaskBodyFilter(u64 layerMask) : m_layerMask(layerMask) {}

		bool ShouldCollideLocked(const JPH::Body& inBody) const override
		{
			Entity* entity = reinterpret_cast<Entity*>(inBody.GetUserData());
			if (!entity) return true;
			return LayerMatchesMask(entity->GetLayer(), m_layerMask);
		}

	private:
		u64 m_layerMask;
	};

	struct ContactPairInfo
	{
		Entity* entity1 = nullptr;
		Entity* entity2 = nullptr;
		bool    isSensor = false;
	};

	// Hash function for body ID pairs - ensures consistent ordering
	SK_FINLINE u64 MakeContactPairKey(JPH::BodyID id1, JPH::BodyID id2)
	{
		u32 rawId1 = id1.GetIndexAndSequenceNumber();
		u32 rawId2 = id2.GetIndexAndSequenceNumber();
		// Ensure consistent ordering for the same pair regardless of order
		if (rawId1 > rawId2) std::swap(rawId1, rawId2);
		return (static_cast<u64>(rawId1) << 32) | static_cast<u64>(rawId2);
	}

	class SkoreContactListener : public JPH::ContactListener
	{
	public:
		moodycamel::ConcurrentQueue<CollisionEvent>* collisionQueue = nullptr;
		std::mutex*                                  contactMapMutex = nullptr;
		HashMap<u64, ContactPairInfo>*               activeContacts = nullptr;

		JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
		{
			Entity* entity1 = reinterpret_cast<Entity*>(inBody1.GetUserData());
			Entity* entity2 = reinterpret_cast<Entity*>(inBody2.GetUserData());

			if (entity1 && entity2)
			{
				if (!Physics::GetLayerCollision(entity1->GetLayer(), entity2->GetLayer()))
				{
					return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
				}
			}

			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			Entity* entity1 = reinterpret_cast<Entity*>(inBody1.GetUserData());
			Entity* entity2 = reinterpret_cast<Entity*>(inBody2.GetUserData());

			if (!entity1 || !entity2) return;

			bool entity1HasCallbacks = entity1->HasFlag(EntityFlags::HasCollisionCallbacks);
			bool entity2HasCallbacks = entity2->HasFlag(EntityFlags::HasCollisionCallbacks);

			if (!entity1HasCallbacks && !entity2HasCallbacks) return;

			bool isSensor = inBody1.IsSensor() || inBody2.IsSensor();
			CollisionEventType eventType = isSensor ? CollisionEventType::TriggerEnter : CollisionEventType::Enter;

			// Track this contact pair for OnContactRemoved
			u64 pairKey = MakeContactPairKey(inBody1.GetID(), inBody2.GetID());
			{
				std::lock_guard<std::mutex> lock(*contactMapMutex);
				activeContacts->Insert(pairKey, ContactPairInfo{entity1, entity2, isSensor});
			}

			JPH::Vec3 contactPoint = inManifold.GetWorldSpaceContactPointOn1(0);
			JPH::Vec3 contactNormal = inManifold.mWorldSpaceNormal;

			collisionQueue->enqueue(CollisionEvent{
				.type = eventType,
				.entity1 = entity1,
				.entity2 = entity2,
				.contactPoint = Cast(contactPoint),
				.contactNormal = Cast(JPH::Vec3(contactNormal)),
				.penetrationDepth = inManifold.mPenetrationDepth
			});
		}

		void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			Entity* entity1 = reinterpret_cast<Entity*>(inBody1.GetUserData());
			Entity* entity2 = reinterpret_cast<Entity*>(inBody2.GetUserData());

			if (!entity1 || !entity2) return;

			bool entity1HasCallbacks = entity1->HasFlag(EntityFlags::HasCollisionCallbacks);
			bool entity2HasCallbacks = entity2->HasFlag(EntityFlags::HasCollisionCallbacks);

			if (!entity1HasCallbacks && !entity2HasCallbacks) return;

			// Only queue Stay events for non-sensors
			if (inBody1.IsSensor() || inBody2.IsSensor()) return;

			JPH::Vec3 contactPoint = inManifold.GetWorldSpaceContactPointOn1(0);
			JPH::Vec3 contactNormal = inManifold.mWorldSpaceNormal;

			collisionQueue->enqueue(CollisionEvent{
				.type = CollisionEventType::Stay,
				.entity1 = entity1,
				.entity2 = entity2,
				.contactPoint = Cast(contactPoint),
				.contactNormal = Cast(JPH::Vec3(contactNormal)),
				.penetrationDepth = inManifold.mPenetrationDepth
			});
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
		{
			u64 pairKey = MakeContactPairKey(inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID());

			ContactPairInfo pairInfo;
			bool found = false;
			{
				std::lock_guard<std::mutex> lock(*contactMapMutex);
				if (auto it = activeContacts->Find(pairKey))
				{
					pairInfo = it->second;
					activeContacts->Erase(it);
					found = true;
				}
			}

			if (!found) return;

			// Check if entities still have callbacks enabled
			bool entity1HasCallbacks = pairInfo.entity1->HasFlag(EntityFlags::HasCollisionCallbacks);
			bool entity2HasCallbacks = pairInfo.entity2->HasFlag(EntityFlags::HasCollisionCallbacks);

			if (!entity1HasCallbacks && !entity2HasCallbacks) return;

			CollisionEventType eventType = pairInfo.isSensor ? CollisionEventType::TriggerExit : CollisionEventType::Exit;

			collisionQueue->enqueue(CollisionEvent{
				.type = eventType,
				.entity1 = pairInfo.entity1,
				.entity2 = pairInfo.entity2,
				.contactPoint = {},
				.contactNormal = {},
				.penetrationDepth = 0.0f
			});
		}
	};

	struct CharacterContactInfo
	{
		JPH::BodyID bodyId;
		Vec3        contactPoint;
		Vec3        contactNormal;
	};

	struct PhysicsScene::Context
	{
		JPH::TempAllocatorImpl tempAllocator = JPH::TempAllocatorImpl(10 * 1024 * 1024);
		JPH::PhysicsSystem     physicsSystem{};
		f32                    stepSize{};

		BroadPhaseLayerInterfaceImpl      broadPhaseLayerInterfaceImpl = {};
		ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilterImpl = {};
		ObjectLayerPairFilterImpl         objectLayerPairFilterImpl = {};

		JPH::JobSystemThreadPool        jobSystem = JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);
		HashSet<JPH::CharacterVirtual*> virtualCharacters;
		Queue<Entity*> requireUpdate;

		// Collision callbacks
		moodycamel::ConcurrentQueue<CollisionEvent> collisionQueue = moodycamel::ConcurrentQueue<CollisionEvent>(256);
		std::mutex                                  contactMapMutex;
		HashMap<u64, ContactPairInfo>               activeContacts;
		SkoreContactListener                        contactListener;

		// CharacterVirtual contact tracking (key: CharacterVirtual*, value: set of body IDs in contact)
		HashMap<JPH::CharacterVirtual*, HashSet<u32>> characterPreviousContacts;

		// Batch body addition
		JPH::BodyIDVector pendingBodiesToAdd;

		// Guard against circular feedback during physics writeback
		bool writingBackTransforms = false;
	};

	RID physicsSettingsRID = {};

	void OnPhysicsSettingsLoaded();

	void OnPhysicsSettingsResourceChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		OnPhysicsSettingsLoaded();
	}

	void OnPhysicsSettingsLoaded()
	{
		RID currentSettings = Settings::Get<ProjectSettings, PhysicsSettings>();
		if (physicsSettingsRID && currentSettings != physicsSettingsRID)
		{
			Resources::GetStorage(physicsSettingsRID)->UnregisterEvent(ResourceEventType::VersionUpdated, OnPhysicsSettingsResourceChanged, nullptr);
			physicsSettingsRID = {};
		}

		if (!physicsSettingsRID)
		{
			physicsSettingsRID = currentSettings;
			Resources::GetStorage(physicsSettingsRID)->RegisterEvent(ResourceEventType::VersionUpdated, OnPhysicsSettingsResourceChanged, nullptr);
		}

		if (ResourceObject settings = Resources::Read(physicsSettingsRID))
		{
			if (settings.HasValue(PhysicsSettings::MaxBodies))
				maxBodies = static_cast<u32>(settings.GetUInt(PhysicsSettings::MaxBodies));
			if (settings.HasValue(PhysicsSettings::MaxBodyPairs))
				maxBodyPairs = static_cast<u32>(settings.GetUInt(PhysicsSettings::MaxBodyPairs));
			if (settings.HasValue(PhysicsSettings::MaxContactConstraints))
				maxContactConstraints = static_cast<u32>(settings.GetUInt(PhysicsSettings::MaxContactConstraints));
			if (settings.HasValue(PhysicsSettings::PhysicsTicksPerSeconds))
				physicsTicksPerSeconds = static_cast<u32>(settings.GetUInt(PhysicsSettings::PhysicsTicksPerSeconds));

			settings.IterateSubObjectList(PhysicsSettings::CollisionMatrix, [&](RID itemRID)
			{
				if (ResourceObject item = Resources::Read(itemRID))
				{
					u64 index = item.GetUInt(CollisionMatrixItem::Index);
					if (index < MaxLayers)
					{
						collisionMatrix[index] = item.GetUInt(CollisionMatrixItem::Value);
					}
				}
			});
		}
	}

	void PhysicsInit()
	{
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		Physics::ResetCollisionMatrix();

		Event::Bind<OnSettingsLoaded, &OnPhysicsSettingsLoaded>();

		debugRenderer = new JoltDebugRenderer();
	}

	void PhysicsShutdown()
	{
		Event::Unbind<OnSettingsLoaded, &OnPhysicsSettingsLoaded>();

		delete debugRenderer;
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}

	PhysicsScene::PhysicsScene()
	{
		if (JPH::Factory::sInstance == nullptr)
		{
			return;
		}

		context = Alloc<Context>();

		if (physicsTicksPerSeconds == 0)
		{
			physicsTicksPerSeconds = 60;
		}

		context->stepSize = 1.0f / (f32)physicsTicksPerSeconds;

		context->physicsSystem.Init(
			maxBodies,
			0,
			maxBodyPairs,
			maxContactConstraints,
			context->broadPhaseLayerInterfaceImpl,
			context->objectVsBroadPhaseLayerFilterImpl,
			context->objectLayerPairFilterImpl);

		// Initialize contact listener
		context->contactListener.collisionQueue = &context->collisionQueue;
		context->contactListener.contactMapMutex = &context->contactMapMutex;
		context->contactListener.activeContacts = &context->activeContacts;
		context->physicsSystem.SetContactListener(&context->contactListener);
	}

	PhysicsScene::~PhysicsScene()
	{
		if (context)
		{
			DestroyAndFree(context);
		}
	}

	struct CollisionShape
	{
		JPH::Ref<JPH::Shape> ref;
	};

	CollisionShapePtr PhysicsScene::CreateStaticMeshShape(Span<Vec3> vertices, Span<u32> indices)
	{
		if (vertices.Size() == 0 || indices.Size() < 3 || (indices.Size() % 3) != 0)
		{
			return {};
		}

		JPH::VertexList vertexList;
		vertexList.reserve(vertices.Size());
		for (usize i = 0; i < vertices.Size(); ++i)
		{
			vertexList.emplace_back(vertices[i].x, vertices[i].y, vertices[i].z);
		}

		JPH::IndexedTriangleList triList;
		const usize triCount = indices.Size() / 3;
		triList.reserve(triCount);
		for (usize i = 0; i < triCount; ++i)
		{
			triList.emplace_back(indices[i * 3 + 0], indices[i * 3 + 1], indices[i * 3 + 2], 0);
		}

		JPH::MeshShapeSettings settings(std::move(vertexList), std::move(triList));
		auto                   created = settings.Create();
		if (created.HasError())
		{
			logger.Warn("Failed to create static mesh shape: {}", created.GetError().c_str());
			return {};
		}

		auto wrapper = std::make_shared<CollisionShape>();
		wrapper->ref = created.Get();
		return wrapper;
	}

	u32 PhysicsScene::AddStaticMeshBody(const CollisionShapePtr& shape, const Vec3& position, const Quat& rotation, u8 layer)
	{
		if (!shape || !shape->ref) return JPH::BodyID::cInvalidBodyID;
		if (!context) return JPH::BodyID::cInvalidBodyID;

		JPH::BodyCreationSettings settings(shape->ref, Cast(position), Cast(rotation), JPH::EMotionType::Static, PhysicsLayers::Encode(layer, false));
		settings.mUserData = 0;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::Body*          body          = bodyInterface.CreateBody(settings);
		if (!body) return JPH::BodyID::cInvalidBodyID;

		context->pendingBodiesToAdd.push_back(body->GetID());
		return body->GetID().GetIndexAndSequenceNumber();
	}

	void PhysicsScene::RemoveStaticBody(u32 bodyHandle)
	{
		if (bodyHandle == JPH::BodyID::cInvalidBodyID) return;
		if (!context) return;

		JPH::BodyID id(bodyHandle);
		if (id.IsInvalid()) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		auto&               pending       = context->pendingBodiesToAdd;
		auto                it            = std::find(pending.begin(), pending.end(), id);
		if (it != pending.end())
		{
			pending.erase(it);
		}
		else
		{
			bodyInterface.RemoveBody(id);
		}
		bodyInterface.DestroyBody(id);
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

				bool validShape = false;
				switch (shape.bodyShape)
				{
					case BodyShapeType::Box:
					{
						if (shape.size.x > 0.01f && shape.size.y > 0.01f && shape.size.z > 0.01f)
						{
							JPH::BoxShapeSettings boxShapeSettings(Cast(shape.size));
							boxShapeSettings.mDensity = shape.density;
							arrShapes.EmplaceBack(boxShapeSettings.Create().Get(), Cast(shape.center));
							validShape = true;
						}
						break;
					}
					case BodyShapeType::Sphere:
					{
						if (shape.radius > 0.01f)
						{
							JPH::SphereShapeSettings sphereShapeSettings(shape.radius);
							sphereShapeSettings.mDensity = shape.density;
							arrShapes.EmplaceBack(sphereShapeSettings.Create().Get(), Cast(shape.center));
							validShape = true;
						}
						break;
					}
					case BodyShapeType::Capsule:
					{
						if (shape.radius > 0.01f && shape.height > 0.01f)
						{
							JPH::CapsuleShapeSettings capsuleShapeSettings(shape.height * 0.5f, shape.radius);
							capsuleShapeSettings.mDensity = shape.density;
							arrShapes.EmplaceBack(capsuleShapeSettings.Create().Get(), Cast(shape.center));
							validShape = true;
						}
						break;
					}
					case BodyShapeType::Cylinder:
					{
						if (shape.radius > 0.01f && shape.height > 0.01f)
						{
							JPH::CylinderShapeSettings cylinderShapeSettings(shape.height * 0.5f, shape.radius);
							cylinderShapeSettings.mDensity = shape.density;
							arrShapes.EmplaceBack(cylinderShapeSettings.Create().Get(), Cast(shape.center));
							validShape = true;
						}
						break;
					}
					default:
						break;
				}

				if (validShape && shape.sensor)
				{
					hasSensor = true;
				}
			}

			if (!arrShapes.Empty())
			{
				JPH::Ref compound = new JPH::StaticCompoundShapeSettings{};
				for (auto& shapeData : arrShapes)
				{
					compound->AddShape(shapeData.center, JPH::QuatArg::sIdentity(), shapeData.shape);
				}
				JPH::RefConst compoundShape = compound->Create().Get();
				JPH::ScaledShapeSettings scaledShapeSettings(compoundShape, Cast(Mat4::GetScale(entity->GetWorldTransform())));
				scaledShape = scaledShapeSettings.Create().Get();
			}
		}

		if (entity->HasFlag(EntityFlags::HasCharacterController))
		{
			if (CharacterController* characterController = entity->GetComponent<CharacterController>())
			{
				if (!scaledShape)
				{
					Vec3 shapeOffset = characterController->GetShapeOffset();
					JPH::Vec3 capsuleOffset(
						shapeOffset.x,
						shapeOffset.y + 0.5f * characterController->GetHeight() + characterController->GetRadius(),
						shapeOffset.z
					);

					scaledShape = JPH::RotatedTranslatedShapeSettings(
						capsuleOffset,
						JPH::Quat::sIdentity(),
						new JPH::CapsuleShape(0.5f * characterController->GetHeight(), characterController->GetRadius())).Create().Get();
				}

				JPH::CharacterVirtualSettings settings{};
				settings.mShape = scaledShape;
				settings.mUp = Cast(characterController->GetUp());
				settings.mSupportingVolume = JPH::Plane(Cast(characterController->GetUp()), -characterController->GetRadius());
				settings.mMaxSlopeAngle = Math::Radians(characterController->GetMaxSlopeAngle());
				settings.mMaxStrength = characterController->GetMaxStrength();
				settings.mMass = characterController->GetMass();
				settings.mPredictiveContactDistance = characterController->GetPredictiveContactDistance();
				settings.mMaxCollisionIterations = characterController->GetMaxCollisionIterations();
				settings.mMaxConstraintIterations = characterController->GetMaxConstraintIterations();
				settings.mMinTimeRemaining = characterController->GetMinTimeRemaining();
				settings.mCollisionTolerance = characterController->GetCollisionTolerance();
				settings.mCharacterPadding = characterController->GetCharacterPadding();
				settings.mMaxNumHits = characterController->GetMaxNumHits();
				settings.mHitReductionCosMaxAngle = characterController->GetHitReductionCosMaxAngle();
				settings.mPenetrationRecoverySpeed = characterController->GetPenetrationRecoverySpeed();

				JPH::CharacterVirtual* characterVirtual = new JPH::CharacterVirtual(&settings,
				                                                                    Cast(Mat4::GetTranslation(entity->GetWorldTransform())),
				                                                                    Cast(Mat4::GetQuaternion(entity->GetWorldTransform())),
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
		bodyCreationSettings.mPosition = Cast(Mat4::GetTranslation(entity->GetWorldTransform()));
		bodyCreationSettings.mRotation = Cast(Mat4::GetQuaternion(entity->GetWorldTransform()));
		bodyCreationSettings.mUserData = PtrToInt(entity);
		bodyCreationSettings.mIsSensor = hasSensor;

		if (RigidBody* rigidBodyComponent = entity->GetComponent<RigidBody>())
		{
			bodyCreationSettings.mAllowDynamicOrKinematic = false;
			bodyCreationSettings.mMotionType = !rigidBodyComponent->IsKinematic() ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic;
			bodyCreationSettings.mObjectLayer = PhysicsLayers::Encode(entity->GetLayer(), true);
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
			bodyCreationSettings.mObjectLayer = PhysicsLayers::Encode(entity->GetLayer(), false);
		}

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		if (JPH::Body* body = bodyInterface.CreateBody(bodyCreationSettings))
		{
			context->pendingBodiesToAdd.push_back(body->GetID());
			entity->m_physicsId = body->GetID().GetIndexAndSequenceNumber();
		}
		else
		{
			logger.Warn("Failed to create physics body");
			entity->m_physicsId = U64_MAX;
		}
	}

	void PhysicsScene::UnregisterPhysicsEntity(Entity* entity)
	{
		if (entity->m_physicsId != U64_MAX)
		{
			// Clean up any active contacts involving this entity before removing the body
			// This prevents stale entity pointers in OnContactRemoved
			{
				std::lock_guard<std::mutex> lock(context->contactMapMutex);
				Array<u64> keysToRemove;
				for (auto it = context->activeContacts.begin(); it != context->activeContacts.end(); ++it)
				{
					if (it->second.entity1 == entity || it->second.entity2 == entity)
					{
						keysToRemove.EmplaceBack(it->first);
					}
				}
				for (u64 key : keysToRemove)
				{
					context->activeContacts.Erase(key);
				}
			}

			if (entity->HasFlag(EntityFlags::HasCharacterController))
			{
				JPH::CharacterVirtual* characterVirtual = static_cast<JPH::CharacterVirtual*>(IntToPtr(entity->m_physicsId));
				context->characterPreviousContacts.Erase(characterVirtual);
				context->virtualCharacters.Erase(characterVirtual);
				delete characterVirtual;
				entity->m_physicsId = U64_MAX;
			}
			else
			{
				JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
				JPH::BodyID         id = JPH::BodyID(entity->m_physicsId);
				auto& pending = context->pendingBodiesToAdd;
				auto it = std::find(pending.begin(), pending.end(), id);
				if (it != pending.end())
				{
					// Body was created but never added to broadphase â€” just destroy it
					pending.erase(it);
				}
				else
				{
					// Body is in the broadphase â€” remove it first
					bodyInterface.RemoveBody(id);
				}
				bodyInterface.DestroyBody(id);
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
		if (entity->m_physicsId == U64_MAX || context->writingBackTransforms) return;

		const Mat4& worldTransform = entity->GetWorldTransform();
		if (entity->HasFlag(EntityFlags::HasCharacterController))
		{
			JPH::CharacterVirtual* characterVirtual = static_cast<JPH::CharacterVirtual*>(IntToPtr(entity->m_physicsId));
			characterVirtual->SetPosition(Cast(Mat4::GetTranslation(worldTransform)));
			characterVirtual->SetRotation(Cast(Mat4::GetQuaternion(worldTransform)));
		}
		else
		{
			JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
			JPH::BodyID id = JPH::BodyID(entity->m_physicsId);

			bodyInterface.SetPositionAndRotation(id, Cast(Mat4::GetTranslation(worldTransform)), Cast(Mat4::GetQuaternion(worldTransform)), JPH::EActivation::DontActivate);
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

	void PhysicsScene::DrawShape(Entity* entity, GPUCommandBuffer* cmd, GPUPipeline* pipeline)
	{
		if (!context) return;
		if (entity->m_physicsId == U64_MAX) return;

		debugRenderer->cmd = cmd;
		debugRenderer->pipeline = pipeline;

		if (entity->HasFlag(EntityFlags::HasCharacterController))
		{
			JPH::CharacterVirtual* characterVirtual = static_cast<JPH::CharacterVirtual*>(IntToPtr(entity->m_physicsId));
			characterVirtual->GetShape()->Draw(debugRenderer, characterVirtual->GetCenterOfMassTransform(), JPH::Vec3Arg(1.0, 1.0, 1.0), {}, false, true);
		}
		else
		{
			JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
			auto id = JPH::BodyID(entity->m_physicsId);
			if (JPH::RefConst<JPH::Shape> shape = bodyInterface.GetShape(id))
			{
				shape->Draw(debugRenderer, bodyInterface.GetCenterOfMassTransform(id), JPH::Vec3Arg(1.0, 1.0, 1.0), {}, false, true);
			}
		}
	}

	void PhysicsScene::ExecuteEvents()
	{
		if (!context) return;

		while (!context->requireUpdate.IsEmpty())
		{
			Entity* entity = context->requireUpdate.Dequeue();
			UnregisterPhysicsEntity(entity);
			RegisterPhysicsEntity(entity);
		}
	}

	f32 PhysicsScene::GetFixedTimeStep() const
	{
		return context ? context->stepSize : 0.0f;
	}

	void PhysicsScene::UpdateCharacterControllers()
	{
		SK_SCOPED_CPU_ZONE("Physics - UpdateCharacterControllers");

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();

		for (JPH::CharacterVirtual* characterVirtual : context->virtualCharacters)
		{
			CharacterController* characterController = static_cast<CharacterController*>(IntToPtr(characterVirtual->GetUserData()));

			characterVirtual->SetUp(Cast(characterController->GetUp()));
			characterVirtual->SetLinearVelocity(Cast(characterController->GetLinearVelocity()));

			characterVirtual->UpdateGroundVelocity();

			JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings{};
			updateSettings.mWalkStairsMinStepForward *= 4.0f;

			f32 frameDt = Math::Clamp(static_cast<f32>(App::DeltaTime()), 0.0001f, 0.1f);

			JPH::Vec3 gravity = context->physicsSystem.GetGravity();
			JPH::Vec3 up = characterVirtual->GetUp();
			JPH::Vec3 velocity = characterVirtual->GetLinearVelocity();
			float verticalSpeed = velocity.Dot(up);
			if (characterVirtual->IsSupported() && verticalSpeed <= 1.0e-3f)
			{
				velocity -= verticalSpeed * up;
			}
			else
			{
				velocity += gravity * frameDt;
			}
			characterVirtual->SetLinearVelocity(velocity);

			JPH::ObjectLayer charObjectLayer = PhysicsLayers::Encode(characterController->entity->GetLayer(), true);
			characterVirtual->ExtendedUpdate(
				frameDt,
				gravity,
				updateSettings,
				context->physicsSystem.GetDefaultBroadPhaseLayerFilter(charObjectLayer),
				context->physicsSystem.GetDefaultLayerFilter(charObjectLayer),
				{},
				{},
				context->tempAllocator);

			// Process character collision callbacks
			Entity* characterEntity = characterController->entity;
			if (characterEntity->HasFlag(EntityFlags::HasCollisionCallbacks))
			{
				HashSet<u32>& previousContacts = context->characterPreviousContacts[characterVirtual];
				HashSet<u32> currentContacts;

				// Get current contacts
				const JPH::CharacterVirtual::ContactList& contacts = characterVirtual->GetActiveContacts();
				for (const JPH::CharacterVirtual::Contact& contact : contacts)
				{
					if (!contact.mBodyB.IsInvalid())
					{
						u32 bodyIdRaw = contact.mBodyB.GetIndexAndSequenceNumber();
						currentContacts.Insert(bodyIdRaw);

						Entity* otherEntity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(contact.mBodyB));
						if (!otherEntity) continue;

						bool isSensor = false;
						if (const JPH::Body* body = context->physicsSystem.GetBodyLockInterfaceNoLock().TryGetBody(contact.mBodyB))
						{
							isSensor = body->IsSensor();
						}
						CollisionEventType enterType = isSensor ? CollisionEventType::TriggerEnter : CollisionEventType::Enter;
						CollisionEventType stayType = CollisionEventType::Stay; // No TriggerStay

						// Check if this is a new contact (Enter) or persistent (Stay)
						if (!previousContacts.Has(bodyIdRaw))
						{
							// New contact - Enter event
							context->collisionQueue.enqueue(CollisionEvent{
								.type = enterType,
								.entity1 = characterEntity,
								.entity2 = otherEntity,
								.contactPoint = Cast(JPH::Vec3(contact.mPosition)),
								.contactNormal = Cast(JPH::Vec3(contact.mContactNormal)),
								.penetrationDepth = contact.mDistance < 0 ? -contact.mDistance : 0.0f
							});
						}
						else if (!isSensor)
						{
							// Persistent contact - Stay event (only for non-sensors)
							context->collisionQueue.enqueue(CollisionEvent{
								.type = stayType,
								.entity1 = characterEntity,
								.entity2 = otherEntity,
								.contactPoint = Cast(JPH::Vec3(contact.mPosition)),
								.contactNormal = Cast(JPH::Vec3(contact.mContactNormal)),
								.penetrationDepth = contact.mDistance < 0 ? -contact.mDistance : 0.0f
							});
						}
					}
				}

				// Find removed contacts (Exit events)
				for (u32 prevBodyId : previousContacts)
				{
					if (!currentContacts.Has(prevBodyId))
					{
						JPH::BodyID bodyId(prevBodyId);
						if (!bodyInterface.IsAdded(bodyId)) continue;

						Entity* otherEntity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(bodyId));
						if (!otherEntity) continue;

						bool wasSensor = false;
						if (const JPH::Body* body = context->physicsSystem.GetBodyLockInterfaceNoLock().TryGetBody(bodyId))
						{
							wasSensor = body->IsSensor();
						}
						CollisionEventType exitType = wasSensor ? CollisionEventType::TriggerExit : CollisionEventType::Exit;

						context->collisionQueue.enqueue(CollisionEvent{
							.type = exitType,
							.entity1 = characterEntity,
							.entity2 = otherEntity,
							.contactPoint = {},
							.contactNormal = {},
							.penetrationDepth = 0.0f
						});
					}
				}

				// Update previous contacts
				previousContacts = std::move(currentContacts);
			}

			if (Transform* transform = characterController->entity->GetComponent<Transform>())
			{
				Vec3 worldPosition = Cast(characterVirtual->GetPosition());
				Quat worldRotation = Cast(characterVirtual->GetRotation());

				Entity* parent = characterController->entity->GetParent();

				context->writingBackTransforms = true;
				if (parent && parent->GetParent()) // Has a non-root parent
				{
					// Convert world transform to local by multiplying with inverse of parent's world transform
					Mat4 worldTransform = Mat4::Translate(Mat4{1.0f}, worldPosition) * Quat::ToMatrix4(worldRotation);
					Mat4 localTransform = Mat4::Inverse(parent->GetWorldTransform()) * worldTransform;

					transform->SetTransform(
						Mat4::GetTranslation(localTransform),
						Mat4::GetQuaternion(localTransform),
						transform->GetScale());
				}
				else
				{
					// No parent (or root parent), world == local
					transform->SetTransform(worldPosition, worldRotation, transform->GetScale());
				}
				context->writingBackTransforms = false;
			}

			characterController->SetLinearVelocity(Cast(characterVirtual->GetLinearVelocity()));
			characterController->SetOnGround(characterVirtual->IsSupported());
		}
	}

	void PhysicsScene::DoFixedUpdate(f32 stepSize)
	{
		SK_SCOPED_CPU_ZONE("Physics - DoFixedUpdate");

		const int collisionSteps = 1;
		context->physicsSystem.Update(
			stepSize,
			collisionSteps,
			&context->tempAllocator,
			&context->jobSystem);
	}

	void PhysicsScene::WriteBackTransforms()
	{
		SK_SCOPED_CPU_ZONE("Physics - WriteBackTransforms");

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();

		JPH::BodyIDVector   outBodyIDs{};
		context->physicsSystem.GetActiveBodies(JPH::EBodyType::RigidBody, outBodyIDs);

		context->writingBackTransforms = true;
		for (const JPH::BodyID bodyId : outBodyIDs)
		{
			JPH::Vec3 position{};
			JPH::Quat rotation{};
			bodyInterface.GetPositionAndRotation(bodyId, position, rotation);

			Entity* entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(bodyId));

			if (Transform* transform = entity->GetComponent<Transform>())
			{
				Vec3 worldPosition = Cast(position);
				Quat worldRotation = Cast(rotation);

				Entity* parent = entity->GetParent();
				if (parent && parent->GetParent()) // Has a non-root parent
				{
					// Convert world transform to local by multiplying with inverse of parent's world transform
					Mat4 worldTransform = Mat4::Translate(Mat4{1.0f}, worldPosition) * Quat::ToMatrix4(worldRotation);
					Mat4 localTransform = Mat4::Inverse(parent->GetWorldTransform()) * worldTransform;

					transform->SetTransform(
						Mat4::GetTranslation(localTransform),
						Mat4::GetQuaternion(localTransform),
						transform->GetScale());
				}
				else
				{
					// No parent (or root parent), world == local
					transform->SetTransform(worldPosition, worldRotation, transform->GetScale());
				}
			}

			if (RigidBody* rigidBodyComponent = entity->GetComponent<RigidBody>())
			{
				rigidBodyComponent->m_linearVelocity = Cast(bodyInterface.GetLinearVelocity(bodyId));
				rigidBodyComponent->m_angularVelocity = Cast(bodyInterface.GetAngularVelocity(bodyId));
			}
		}
		context->writingBackTransforms = false;
	}

	void PhysicsScene::OnSceneActivated() {}

	void PhysicsScene::OnSceneDeactivated() {}

	JPH::PhysicsSystem* PhysicsScene::GetPhysicsSystem()
	{
		return &context->physicsSystem;
	}

	void PhysicsScene::SetLinearVelocity(Entity* entity, const Vec3& velocity)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.SetLinearVelocity(bodyId, Cast(velocity));
	}

	void PhysicsScene::SetAngularVelocity(Entity* entity, const Vec3& velocity)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.SetAngularVelocity(bodyId, Cast(velocity));
	}

	void PhysicsScene::AddForce(Entity* entity, const Vec3& force)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.AddForce(bodyId, Cast(force));
	}

	void PhysicsScene::AddForceAtPosition(Entity* entity, const Vec3& force, const Vec3& position)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.AddForce(bodyId, Cast(force), Cast(position));
	}

	void PhysicsScene::AddImpulse(Entity* entity, const Vec3& impulse)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.AddImpulse(bodyId, Cast(impulse));
	}

	void PhysicsScene::AddImpulseAtPosition(Entity* entity, const Vec3& impulse, const Vec3& position)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.AddImpulse(bodyId, Cast(impulse), Cast(position));
	}

	void PhysicsScene::AddTorque(Entity* entity, const Vec3& torque)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.AddTorque(bodyId, Cast(torque));
	}

	void PhysicsScene::AddAngularImpulse(Entity* entity, const Vec3& angularImpulse)
	{
		if (entity->m_physicsId == U64_MAX || entity->HasFlag(EntityFlags::HasCharacterController)) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();
		JPH::BodyID bodyId = JPH::BodyID(entity->m_physicsId);
		bodyInterface.AddAngularImpulse(bodyId, Cast(angularImpulse));
	}

	void PhysicsScene::RegisterCollisionCallbacks(Entity* entity)
	{
		entity->AddFlag(EntityFlags::HasCollisionCallbacks);
	}

	void PhysicsScene::UnregisterCollisionCallbacks(Entity* entity)
	{
		entity->RemoveFlag(EntityFlags::HasCollisionCallbacks);
	}

	void PhysicsScene::ProcessCollisionEvents()
	{
		SK_SCOPED_CPU_ZONE("Physics - Process Collision Events");

		CollisionEvent event;
		while (context->collisionQueue.try_dequeue(event))
		{
			auto dispatchToEntity = [&](Entity* selfEntity, Entity* otherEntity, const Vec3& normal)
			{
				if (!selfEntity->HasFlag(EntityFlags::HasCollisionCallbacks)) return;

				Collision collision;
				collision.entity = otherEntity;
				collision.selfEntity = selfEntity;
				collision.contactPoint = event.contactPoint;
				collision.contactNormal = normal;
				collision.penetrationDepth = event.penetrationDepth;

				for (Component* component : selfEntity->GetComponents())
				{
					CollisionListener* listener = dynamic_cast<CollisionListener*>(component);
					if (!listener) continue;

					switch (event.type)
					{
						case CollisionEventType::Enter:
							listener->OnCollisionEnter(collision);
							break;
						case CollisionEventType::Stay:
							listener->OnCollisionStay(collision);
							break;
						case CollisionEventType::Exit:
							listener->OnCollisionExit(collision);
							break;
						case CollisionEventType::TriggerEnter:
							listener->OnTriggerEnter(collision);
							break;
						case CollisionEventType::TriggerExit:
							listener->OnTriggerExit(collision);
							break;
					}
				}
			};

			// Dispatch to both entities involved in the collision
			dispatchToEntity(event.entity1, event.entity2, event.contactNormal);
			dispatchToEntity(event.entity2, event.entity1, -event.contactNormal);
		}
	}

	void PhysicsScene::ProcessPendingBodiesToAdd()
	{
		SK_SCOPED_CPU_ZONE("Physics - Process Pending Bodies To Add");

		if (context->pendingBodiesToAdd.empty()) return;

		JPH::BodyInterface& bodyInterface = context->physicsSystem.GetBodyInterface();

		JPH::BodyInterface::AddState addState = bodyInterface.AddBodiesPrepare(
			context->pendingBodiesToAdd.data(),
			static_cast<int>(context->pendingBodiesToAdd.size()));

		bodyInterface.AddBodiesFinalize(
			context->pendingBodiesToAdd.data(),
			static_cast<int>(context->pendingBodiesToAdd.size()),
			addState,
			JPH::EActivation::Activate);

		context->pendingBodiesToAdd.clear();
	}


	void Physics::SetCurrentScene(PhysicsScene* physicsScene)
	{
		currentPhysicsScene = physicsScene;
	}

	PhysicsScene* Physics::GetCurrentScene()
	{
		return currentPhysicsScene;
	}

	bool Physics::Raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask)
	{
		if (!currentPhysicsScene) return false;

		JPH::PhysicsSystem* physicsSystem = currentPhysicsScene->GetPhysicsSystem();
		if (!physicsSystem) return false;

		JPH::RRayCast ray{Cast(origin), Cast(direction * maxDistance)};
		JPH::RayCastResult result;

		LayerMaskBodyFilter bodyFilter(layerMask);

		if (physicsSystem->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, bodyFilter))
		{
			JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
			JPH::BodyID bodyId = result.mBodyID;

			hit.distance = result.mFraction * maxDistance;
			hit.point = origin + direction * hit.distance;

			JPH::RVec3 surfaceNormal = bodyInterface.GetShape(bodyId)->GetSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction));
			hit.normal = Cast(JPH::Vec3(surfaceNormal));

			hit.entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(bodyId));

			return true;
		}

		return false;
	}

	bool Physics::RaycastAll(const Vec3& origin, const Vec3& direction, f32 maxDistance, Array<RaycastHit>& hits, u64 layerMask)
	{
		if (!currentPhysicsScene) return false;

		JPH::PhysicsSystem* physicsSystem = currentPhysicsScene->GetPhysicsSystem();
		if (!physicsSystem) return false;

		JPH::RRayCast ray{Cast(origin), Cast(direction * maxDistance)};
		JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;

		LayerMaskBodyFilter bodyFilter(layerMask);

		physicsSystem->GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings{}, collector, {}, {}, bodyFilter);

		if (!collector.HadHit())
		{
			return false;
		}

		collector.Sort();

		JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

		for (const JPH::RayCastResult& result : collector.mHits)
		{
			RaycastHit hit;
			hit.distance = result.mFraction * maxDistance;
			hit.point = origin + direction * hit.distance;

			JPH::RVec3 surfaceNormal = bodyInterface.GetShape(result.mBodyID)->GetSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction));
			hit.normal = Cast(JPH::Vec3(surfaceNormal));

			hit.entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(result.mBodyID));

			hits.EmplaceBack(hit);
		}

		return true;
	}

	bool Physics::SphereCast(const Vec3& origin, f32 radius, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask)
	{
		if (!currentPhysicsScene) return false;

		JPH::PhysicsSystem* physicsSystem = currentPhysicsScene->GetPhysicsSystem();
		if (!physicsSystem) return false;

		JPH::SphereShape sphereShape(radius);
		JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
			&sphereShape,
			JPH::Vec3::sReplicate(1.0f),
			JPH::RMat44::sTranslation(Cast(origin)),
			Cast(direction * maxDistance)
		);

		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

		LayerMaskBodyFilter bodyFilter(layerMask);

		physicsSystem->GetNarrowPhaseQuery().CastShape(shapeCast, settings, JPH::RVec3(Cast(origin)), collector, {}, {}, bodyFilter);

		if (!collector.HadHit())
		{
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
		const JPH::ShapeCastResult& result = collector.mHit;

		hit.distance = result.mFraction * maxDistance;
		hit.point = Cast(JPH::Vec3(result.mContactPointOn2));
		hit.normal = Cast(JPH::Vec3(-result.mPenetrationAxis.Normalized()));
		hit.entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(result.mBodyID2));

		return true;
	}

	bool Physics::BoxCast(const Vec3& origin, const Vec3& halfExtents, const Quat& orientation, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask)
	{
		if (!currentPhysicsScene) return false;

		JPH::PhysicsSystem* physicsSystem = currentPhysicsScene->GetPhysicsSystem();
		if (!physicsSystem) return false;

		JPH::BoxShape boxShape(Cast(halfExtents));
		JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
			&boxShape,
			JPH::Vec3::sReplicate(1.0f),
			JPH::RMat44::sRotationTranslation(Cast(orientation), Cast(origin)),
			Cast(direction * maxDistance)
		);

		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

		LayerMaskBodyFilter bodyFilter(layerMask);

		physicsSystem->GetNarrowPhaseQuery().CastShape(shapeCast, settings, JPH::RVec3(Cast(origin)), collector, {}, {}, bodyFilter);

		if (!collector.HadHit())
		{
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
		const JPH::ShapeCastResult& result = collector.mHit;

		hit.distance = result.mFraction * maxDistance;
		hit.point = Cast(JPH::Vec3(result.mContactPointOn2));
		hit.normal = Cast(JPH::Vec3(-result.mPenetrationAxis.Normalized()));
		hit.entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(result.mBodyID2));

		return true;
	}

	bool Physics::CapsuleCast(const Vec3& origin, f32 radius, f32 halfHeight, const Quat& orientation, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask)
	{
		if (!currentPhysicsScene) return false;

		JPH::PhysicsSystem* physicsSystem = currentPhysicsScene->GetPhysicsSystem();
		if (!physicsSystem) return false;

		JPH::CapsuleShape capsuleShape(halfHeight, radius);
		JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
			&capsuleShape,
			JPH::Vec3::sReplicate(1.0f),
			JPH::RMat44::sRotationTranslation(Cast(orientation), Cast(origin)),
			Cast(direction * maxDistance)
		);

		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

		LayerMaskBodyFilter bodyFilter(layerMask);

		physicsSystem->GetNarrowPhaseQuery().CastShape(shapeCast, settings, JPH::RVec3(Cast(origin)), collector, {}, {}, bodyFilter);

		if (!collector.HadHit())
		{
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
		const JPH::ShapeCastResult& result = collector.mHit;

		hit.distance = result.mFraction * maxDistance;
		hit.point = Cast(JPH::Vec3(result.mContactPointOn2));
		hit.normal = Cast(JPH::Vec3(-result.mPenetrationAxis.Normalized()));
		hit.entity = reinterpret_cast<Entity*>(bodyInterface.GetUserData(result.mBodyID2));

		return true;
	}

	void Physics::SetLayerCollision(u8 layerA, u8 layerB, bool shouldCollide)
	{
		if (layerA >= MaxLayers || layerB >= MaxLayers) return;

		if (shouldCollide)
		{
			collisionMatrix[layerA] |= (1ULL << layerB);
			collisionMatrix[layerB] |= (1ULL << layerA);
		}
		else
		{
			collisionMatrix[layerA] &= ~(1ULL << layerB);
			collisionMatrix[layerB] &= ~(1ULL << layerA);
		}
	}

	bool Physics::GetLayerCollision(u8 layerA, u8 layerB)
	{
		if (layerA >= MaxLayers || layerB >= MaxLayers) return false;
		return (collisionMatrix[layerA] & (1ULL << layerB)) != 0;
	}

	u64 Physics::GetCollisionMask(u8 layer)
	{
		if (layer >= MaxLayers) return 0;
		return collisionMatrix[layer];
	}

	void Physics::ResetCollisionMatrix()
	{
		for (u32 i = 0; i < MaxLayers; ++i)
		{
			collisionMatrix[i] = AllLayersMask;
		}
	}

	void Physics::RegisterType(NativeReflectType<Physics>& type)
	{
		type.Function<&Physics::Raycast>("Raycast", "origin", "direction", "maxDistance", "hit", "layerMask");
		type.Function<&Physics::RaycastAll>("RaycastAll", "origin", "direction", "maxDistance", "hits", "layerMask");
		type.Function<&Physics::SphereCast>("SphereCast", "origin", "radius", "direction", "maxDistance", "hit", "layerMask");
		type.Function<&Physics::BoxCast>("BoxCast", "origin", "halfExtents", "orientation", "direction", "maxDistance", "hit", "layerMask");
		type.Function<&Physics::CapsuleCast>("CapsuleCast", "origin", "radius", "halfHeight", "orientation", "direction", "maxDistance", "hit", "layerMask");

		type.Function<&Physics::SetLayerCollision>("SetLayerCollision", "layerA", "layerB", "shouldCollide");
		type.Function<&Physics::GetLayerCollision>("GetLayerCollision", "layerA", "layerB");
		type.Function<&Physics::GetCollisionMask>("GetCollisionMask", "layer");
		type.Function<&Physics::ResetCollisionMatrix>("ResetCollisionMatrix");
	}
}