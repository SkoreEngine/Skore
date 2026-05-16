#include "Skore/Navigation/Components/NavMeshAgent.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Navigation/Navigation.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::NavMeshAgent");

	void NavMeshAgent::Create()
	{
	}

	void NavMeshAgent::OnStart()
	{
		m_transform = entity->GetComponent<Transform>();

		if (!scene->navigationScene.HasNavMesh())
		{
			logger.Warn("NavMeshAgent started but no navmesh is available");
			return;
		}

		Vec3 worldPos = entity->GetWorldPosition();
		m_agentIndex = scene->navigationScene.AddAgent(worldPos, m_radius, m_height, m_speed, m_acceleration);

		if (m_agentIndex < 0)
		{
			logger.Error("Failed to add agent to crowd");
		}
	}

	void NavMeshAgent::OnUpdate(f64 deltaTime)
	{
		if (m_agentIndex < 0 || !m_transform) return;

		Vec3 agentPos = scene->navigationScene.GetAgentPosition(m_agentIndex);

		// Convert world position to local space (same pattern as CharacterController in Physics.cpp)
		Entity* parent = entity->GetParent();
		if (parent && parent->GetParent()) // Has a non-root parent
		{
			Mat4 worldTransform = Mat4::Translate(Mat4{1.0f}, agentPos);
			Mat4 localTransform = Mat4::Inverse(parent->GetWorldTransform()) * worldTransform;
			m_transform->SetPosition(Mat4::GetTranslation(localTransform));
		}
		else
		{
			m_transform->SetPosition(agentPos);
		}
	}

	void NavMeshAgent::Destroy()
	{
		if (m_agentIndex >= 0 && scene)
		{
			scene->navigationScene.RemoveAgent(m_agentIndex);
			m_agentIndex = -1;
		}
	}

	void NavMeshAgent::SetDestination(const Vec3& target)
	{
		m_destination = target;
		m_hasDestination = true;

		if (m_agentIndex >= 0)
		{
			scene->navigationScene.SetAgentTarget(m_agentIndex, target);
		}
	}

	Vec3 NavMeshAgent::GetDestination() const
	{
		return m_destination;
	}

	Vec3 NavMeshAgent::GetVelocity() const
	{
		if (m_agentIndex < 0) return Vec3{};
		return scene->navigationScene.GetAgentVelocity(m_agentIndex);
	}

	bool NavMeshAgent::IsAtDestination() const
	{
		if (!m_hasDestination || m_agentIndex < 0) return false;
		return GetRemainingDistance() <= m_stoppingDistance;
	}

	f32 NavMeshAgent::GetRemainingDistance() const
	{
		if (!m_hasDestination || m_agentIndex < 0) return 0.0f;
		Vec3 agentPos = scene->navigationScene.GetAgentPosition(m_agentIndex);
		return Vec3::Length(m_destination - agentPos);
	}

	f32 NavMeshAgent::GetSpeed() const
	{
		return m_speed;
	}

	void NavMeshAgent::SetSpeed(f32 speed)
	{
		m_speed = speed;
	}

	f32 NavMeshAgent::GetAcceleration() const
	{
		return m_acceleration;
	}

	void NavMeshAgent::SetAcceleration(f32 acceleration)
	{
		m_acceleration = acceleration;
	}

	f32 NavMeshAgent::GetRadius() const
	{
		return m_radius;
	}

	void NavMeshAgent::SetRadius(f32 radius)
	{
		m_radius = radius;
	}

	f32 NavMeshAgent::GetHeight() const
	{
		return m_height;
	}

	void NavMeshAgent::SetHeight(f32 height)
	{
		m_height = height;
	}

	f32 NavMeshAgent::GetStoppingDistance() const
	{
		return m_stoppingDistance;
	}

	void NavMeshAgent::SetStoppingDistance(f32 stoppingDistance)
	{
		m_stoppingDistance = stoppingDistance;
	}

	void NavMeshAgent::RegisterType(NativeReflectType<NavMeshAgent>& type)
	{
		type.Field<&NavMeshAgent::m_speed>("speed");
		type.Field<&NavMeshAgent::m_acceleration>("acceleration");
		type.Field<&NavMeshAgent::m_radius>("radius");
		type.Field<&NavMeshAgent::m_height>("height");
		type.Field<&NavMeshAgent::m_stoppingDistance>("stoppingDistance");

		type.Function<&NavMeshAgent::SetDestination>("SetDestination", "target");
		type.Function<&NavMeshAgent::GetDestination>("GetDestination");
		type.Function<&NavMeshAgent::GetVelocity>("GetVelocity");
		type.Function<&NavMeshAgent::IsAtDestination>("IsAtDestination");
		type.Function<&NavMeshAgent::GetRemainingDistance>("GetRemainingDistance");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Navigation"});
	}
}