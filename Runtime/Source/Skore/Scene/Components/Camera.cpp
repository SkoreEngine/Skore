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

#include "Camera.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
{
	void Camera::Create(ComponentSettings& settings)
	{
		m_renderStorage = GetScene()->GetRenderStorage();
		m_renderStorage->RegisterCamera(this, entity->GetRID().id);
		m_renderStorage->SetCameraViewMatrix(this, Math::Inverse(entity->GetGlobalTransform()));
		m_renderStorage->SetCameraPosition(this, entity->GetWorldPosition());
		m_renderStorage->SetCameraProjection(this, m_projection);
		m_renderStorage->SetCameraFov(this, m_fov);
		m_renderStorage->SetCameraNear(this, m_near);
		m_renderStorage->SetCameraFar(this, m_far);
	}

	void Camera::Destroy()
	{
		m_renderStorage->RemoveCamera(this);
	}

	Camera::Projection Camera::GetProjection() const
	{
		return m_projection;
	}

	void Camera::SetProjection(Projection projection)
	{
		m_projection = projection;

		if (m_renderStorage)
		{
			m_renderStorage->SetCameraProjection(this, m_projection);
		}
	}

	f32 Camera::GetFov() const
	{
		return m_fov;
	}

	void Camera::SetFov(f32 fov)
	{
		m_fov = fov;
		if (m_renderStorage)
		{
			m_renderStorage->SetCameraFov(this, m_fov);
		}
	}

	f32 Camera::GetNear() const
	{
		return m_near;
	}

	void Camera::SetNear(f32 near)
	{
		m_near = near;
		if (m_renderStorage)
		{
			m_renderStorage->SetCameraNear(this, m_near);
		}
	}

	f32 Camera::GetFar() const
	{
		return m_far;
	}

	void Camera::SetFar(f32 far)
	{
		m_far = far;
		if (m_renderStorage)
		{
			m_renderStorage->SetCameraFar(this, m_far);
		}
	}

	void Camera::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_renderStorage) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_renderStorage->SetCameraVisible(this, true);
				break;
			case EntityEventType::EntityDeactivated:
				m_renderStorage->SetCameraVisible(this, false);
				break;
			case EntityEventType::TransformUpdated:
				m_renderStorage->SetCameraViewMatrix(this, Math::Inverse(entity->GetGlobalTransform()));
				m_renderStorage->SetCameraPosition(this, entity->GetWorldPosition());
				break;
		}
	}

	void Camera::RegisterType(NativeReflectType<Camera>& type)
	{
		type.Field<&Camera::m_projection>("projection");
		type.Field<&Camera::m_fov>("fov");
		type.Field<&Camera::m_near>("near");
		type.Field<&Camera::m_far>("far");
	}

}
