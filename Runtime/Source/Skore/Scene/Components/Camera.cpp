#include "Skore/Scene/Components/Camera.hpp"

#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
{
	void Camera::Create(ComponentSettings& settings)
	{
		cameraObject = scene->renderObjects.CreateCameraObject();
		cameraObject->SetTransform(entity->GetWorldTransform());
		cameraObject->SetVisible(entity->IsActive());
		cameraObject->SetUserData(PtrToInt(entity));
		cameraObject->SetCullingMask(m_cullingMask);

		if (RenderPipelineContext* context = RenderPipeline::GetMainContext())
		{
			context->UpdateCamera(m_near, m_far, m_fov, m_projection, {Mat4::Inverse(entity->GetWorldTransform())}, entity->GetWorldPosition());
			context->camera.cullingMask = m_cullingMask;
		}
	}

	void Camera::Destroy()
	{
		if (cameraObject)
		{
			cameraObject->Destroy();
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
		if (cameraObject) cameraObject->SetCullingMask(cullingMask);
	}

	void Camera::ProcessEvent(const EntityEventDesc& event)
	{
		if (!cameraObject) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				cameraObject->SetVisible(true);
				break;
			case EntityEventType::EntityDeactivated:
				cameraObject->SetVisible(false);
			case EntityEventType::TransformUpdated:
				cameraObject->SetTransform(entity->GetWorldTransform());

				if (RenderPipelineContext* context = RenderPipeline::GetMainContext())
				{
					context->UpdateCamera(m_near, m_far, m_fov, m_projection, {Mat4::Inverse(entity->GetWorldTransform())}, entity->GetWorldPosition());
					context->camera.cullingMask = m_cullingMask;
				}
				break;
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
	}

}