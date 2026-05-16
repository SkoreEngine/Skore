#pragma once

#include "Skore/Core/Math.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	class Transform;

	class SK_API NavMeshAgent : public Component, public Tickable
	{
	public:
		SK_CLASS(NavMeshAgent, Component);

		void Create() override;
		void OnStart() override;
		void OnUpdate(f64 deltaTime) override;
		void Destroy() override;

		void SetDestination(const Vec3& target);
		Vec3 GetDestination() const;
		Vec3 GetVelocity() const;
		bool IsAtDestination() const;
		f32  GetRemainingDistance() const;

		f32  GetSpeed() const;
		void SetSpeed(f32 speed);
		f32  GetAcceleration() const;
		void SetAcceleration(f32 acceleration);
		f32  GetRadius() const;
		void SetRadius(f32 radius);
		f32  GetHeight() const;
		void SetHeight(f32 height);
		f32  GetStoppingDistance() const;
		void SetStoppingDistance(f32 stoppingDistance);

		static void RegisterType(NativeReflectType<NavMeshAgent>& type);

	private:
		f32 m_speed = 3.5f;
		f32 m_acceleration = 8.0f;
		f32 m_radius = 0.5f;
		f32 m_height = 2.0f;
		f32 m_stoppingDistance = 0.1f;

		i32 m_agentIndex = -1;
		Vec3 m_destination{};
		bool m_hasDestination = false;
		Transform* m_transform = nullptr;
	};
}
