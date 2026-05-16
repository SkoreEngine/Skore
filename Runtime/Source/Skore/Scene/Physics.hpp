#pragma once

#include "Entity.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"

namespace JPH
{
	class PhysicsSystem;
}

namespace Skore
{
	class GPUCommandBuffer;
	class GPUPipeline;

	constexpr u32 DebugPhysicsVertexSize = 36;

	enum class CollisionDetectionType
	{
		Discrete,
		LinearCast
	};

	struct CollisionMatrixItem
	{
		enum
		{
			Index,//Uint
			Value //Uint
		};

	};

	struct PhysicsSettings
	{
		enum
		{
			MaxBodies,              //UInt
			MaxBodyPairs,           //UInt
			MaxContactConstraints,  //UInt
			PhysicsTicksPerSeconds, //UInt
			CollisionMatrix,			  //Subobject
		};
	};

	enum class BodyShapeType
	{
		None     = 0,
		Plane    = 1,
		Box      = 2,
		Sphere   = 3,
		Capsule  = 4,
		Cylinder = 5,
		Mesh     = 6,
		Convex   = 7,
		Terrain  = 8
	};

	struct BodyShapeBuilder
	{
		BodyShapeType bodyShape = BodyShapeType::None;
		Vec3          size{1.0f, 1.0f, 1.0f};
		Vec3          center{0.0f, 0.0f, 0.0f};
		f32           height{1.0};
		f32           radius{0.5};
		f32           density = 1000;
		bool          sensor = false;
	};

	struct ShapeCollector
	{
		Array<BodyShapeBuilder> shapes;
	};

	class SK_API PhysicsScene
	{
	public:
		struct Context;
		SK_NO_COPY_CONSTRUCTOR(PhysicsScene);

		PhysicsScene();
		~PhysicsScene();

		f32 GetFixedTimeStep() const;

		void RegisterPhysicsEntity(Entity* entity);
		void UnregisterPhysicsEntity(Entity* entity);
		void PhysicsEntityRequireUpdate(Entity* entity);
		void UpdateTransform(Entity* entity);

		void DrawEntities(GPUCommandBuffer* cmd, GPUPipeline* pipeline, const HashSet<Entity*>& entities);
		void DrawShape(Entity* entity, GPUCommandBuffer* cmd, GPUPipeline* pipeline);

		JPH::PhysicsSystem* GetPhysicsSystem();

		void SetLinearVelocity(Entity* entity, const Vec3& velocity);
		void SetAngularVelocity(Entity* entity, const Vec3& velocity);
		void AddForce(Entity* entity, const Vec3& force);
		void AddForceAtPosition(Entity* entity, const Vec3& force, const Vec3& position);
		void AddImpulse(Entity* entity, const Vec3& impulse);
		void AddImpulseAtPosition(Entity* entity, const Vec3& impulse, const Vec3& position);
		void AddTorque(Entity* entity, const Vec3& torque);
		void AddAngularImpulse(Entity* entity, const Vec3& angularImpulse);

		void RegisterCollisionCallbacks(Entity* entity);
		void UnregisterCollisionCallbacks(Entity* entity);
		void ProcessCollisionEvents();
		void ProcessPendingBodiesToAdd();

		friend class Scene;
		friend class Physics;
	private:
		Context* context = nullptr;

		void ExecuteEvents();
		void UpdateCharacterControllers();
		void DoFixedUpdate(f32 stepSize);
		void WriteBackTransforms();
		void OnSceneActivated();
		void OnSceneDeactivated();
	};

	struct RaycastHit
	{
		Entity* entity = nullptr;
		Vec3    point{};
		Vec3    normal{};
		f32     distance = 0.0f;
	};

	enum class CollisionEventType : u8
	{
		Enter,
		Stay,
		Exit,
		TriggerEnter,
		TriggerExit
	};

	struct CollisionEvent
	{
		CollisionEventType type;
		Entity*            entity1 = nullptr;
		Entity*            entity2 = nullptr;
		Vec3               contactPoint{};
		Vec3               contactNormal{};
		f32                penetrationDepth = 0.0f;
	};

	class SK_API Physics
	{
	public:
		static void SetCurrentScene(PhysicsScene* physicsScene);
		static PhysicsScene* GetCurrentScene();

		static bool Raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask = AllLayersMask);
		static bool RaycastAll(const Vec3& origin, const Vec3& direction, f32 maxDistance, Array<RaycastHit>& hits, u64 layerMask = AllLayersMask);

		static bool SphereCast(const Vec3& origin, f32 radius, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask = AllLayersMask);
		static bool BoxCast(const Vec3& origin, const Vec3& halfExtents, const Quat& orientation, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask = AllLayersMask);
		static bool CapsuleCast(const Vec3& origin, f32 radius, f32 halfHeight, const Quat& orientation, const Vec3& direction, f32 maxDistance, RaycastHit& hit, u64 layerMask = AllLayersMask);

		// Layer Collision Matrix
		static void SetLayerCollision(u8 layerA, u8 layerB, bool shouldCollide);
		static bool GetLayerCollision(u8 layerA, u8 layerB);
		static u64  GetCollisionMask(u8 layer);
		static void ResetCollisionMatrix();

		static void RegisterType(NativeReflectType<Physics>& type);
	};
}
