#pragma once

#include "SceneCommon.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class Scene;
	class Entity;
	class PhysicsScene;

	struct Collision
	{
		Entity* entity = nullptr;        // The other entity involved in the collision
		Entity* selfEntity = nullptr;    // Our entity
		Vec3    contactPoint{};          // World-space contact point
		Vec3    contactNormal{};         // Contact normal (pointing from other to self)
		f32     penetrationDepth = 0.0f; // Penetration depth
	};

	class SK_API Component : public Object
	{
	public:
		SK_CLASS(Component, Object);

		Entity* entity = nullptr;
		Scene* scene = nullptr;

		RID GetRID() const;
		Entity* GetEntity() const { return entity; }
		Scene* GetScene() const { return scene; }

		//called after construction / deserialization
		virtual void Create() {}

		//called before destruction
		virtual void Destroy() {}
		virtual void OnStart() {}

		virtual void ProcessEvent(const EntityEventDesc& event) {}

		void PhysicsRequireUpdate() const;

		static void RegisterType(NativeReflectType<Component>& type);

		friend class Entity;
		friend class PhysicsScene;
	private:
		RID m_rid;
		u32 m_version = 0;

		void RegisterEvents();
		void RemoveEvents();
	};

	class SK_API Tickable
	{
	public:
		virtual ~Tickable() = default;
		virtual void OnUpdate(f64 deltaTime) = 0;
	};

	class SK_API FixedTickable
	{
	public:
		virtual ~FixedTickable() = default;
		virtual void OnFixedUpdate(f64 fixedDeltaTime) = 0;
	};

	class SK_API CollisionListener
	{
	public:
		virtual ~CollisionListener() = default;
		virtual void OnCollisionEnter(const Collision& collision) {}
		virtual void OnCollisionStay(const Collision& collision) {}
		virtual void OnCollisionExit(const Collision& collision) {}
		virtual void OnTriggerEnter(const Collision& collision) {}
		virtual void OnTriggerExit(const Collision& collision) {}
	};

}
