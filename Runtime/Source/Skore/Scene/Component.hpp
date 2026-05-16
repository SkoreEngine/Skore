#pragma once

#include "SceneCommon.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class Scene;
	class Entity;
	class PhysicsScene;

	struct ComponentSettings
	{
		bool enableUpdate = false;
		bool enableFixedUpdate = false;
		bool enableCollisionCallbacks = false;
	};

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
		virtual void Create(ComponentSettings& settings) {}

		//called before destruction
		virtual void Destroy() {}
		virtual void OnStart() {}
		virtual void OnUpdate(f64 deltaTime) {}
		virtual void OnFixedUpdate() {}

		virtual void OnCollisionEnter(const Collision& collision) {}
		virtual void OnCollisionStay(const Collision& collision) {}
		virtual void OnCollisionExit(const Collision& collision) {}
		virtual void OnTriggerEnter(const Collision& collision) {}
		virtual void OnTriggerExit(const Collision& collision) {}

		virtual void ProcessEvent(const EntityEventDesc& event) {}

		void PhysicsRequireUpdate() const;

		static void RegisterType(NativeReflectType<Component>& type);

		friend class Entity;
		friend class PhysicsScene;
	private:
		RID m_rid;
		ComponentSettings m_settings = {};
		u32 m_version = 0;

		void RegisterEvents();
		void RemoveEvents();
	};
}
