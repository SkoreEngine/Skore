#include "Skore/Scene/Components/Camera.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/DebugDraw.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Scene/Entity.hpp"


namespace Skore
{
	void Camera::OnCreate()
	{
		if (RenderPipelineContext* context = RenderPipeline::GetMainContext())
		{
			context->UpdateCamera(m_near, m_far, m_fov, m_projection, {Mat4::Inverse(entity->GetWorldTransform())}, entity->GetWorldPosition());
			context->camera.cullingMask = m_cullingMask;
		}
	}

	Projection Camera::GetProjection() const
	{
		return m_projection;
	}

	void Camera::SetProjection(Projection projection)
	{
		m_projection = projection;
	}

	f32 Camera::GetFov() const
	{
		return m_fov;
	}

	void Camera::SetFov(f32 fov)
	{
		m_fov = fov;
	}

	f32 Camera::GetNear() const
	{
		return m_near;
	}

	void Camera::SetNear(f32 near)
	{
		m_near = near;
	}

	f32 Camera::GetFar() const
	{
		return m_far;
	}

	void Camera::SetFar(f32 far)
	{
		m_far = far;
	}

	u64 Camera::GetCullingMask() const
	{
		return m_cullingMask;
	}

	void Camera::SetCullingMask(u64 cullingMask)
	{
		m_cullingMask = cullingMask;
	}

	void Camera::OnUpdate(f64 deltaTime)
	{
		if (RenderPipelineContext* context = RenderPipeline::GetMainContext())
		{
			context->UpdateCamera(m_near, m_far, m_fov, m_projection, {Mat4::Inverse(entity->GetWorldTransform())}, entity->GetWorldPosition());
			context->camera.cullingMask = m_cullingMask;
		}
	}

	void Camera::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::DrawGizmos)
		{
			DrawGizmosEvent* data = static_cast<DrawGizmosEvent*>(event.eventData);
			data->debugDraw->DrawCameraFrustum(entity->GetWorldTransform(), m_fov, data->viewportAspect, m_near, m_far, 1.0f, Color::WHITE);
		}
	}

	void Camera::RegisterType(NativeReflectType<Camera>& type)
	{
		type.Field<&Camera::m_projection>("projection");
		type.Field<&Camera::m_fov>("fov");
		type.Field<&Camera::m_near>("near");
		type.Field<&Camera::m_far>("far");
		type.Field<&Camera::m_cullingMask, &Camera::GetCullingMask, &Camera::SetCullingMask>("cullingMask")
			.Attribute<UILayerMaskProperty>();

		type.Function<&Camera::GetProjection>("GetProjection");
		type.Function<&Camera::SetProjection>("SetProjection", "projection");
		type.Function<&Camera::GetFov>("GetFov");
		type.Function<&Camera::SetFov>("SetFov", "fov");
		type.Function<&Camera::GetNear>("GetNear");
		type.Function<&Camera::SetNear>("SetNear", "near");
		type.Function<&Camera::GetFar>("GetFar");
		type.Function<&Camera::SetFar>("SetFar", "far");
		type.Function<&Camera::GetCullingMask>("GetCullingMask");
		type.Function<&Camera::SetCullingMask>("SetCullingMask", "cullingMask");

		type.Attribute<Iterable>();
	}

}
