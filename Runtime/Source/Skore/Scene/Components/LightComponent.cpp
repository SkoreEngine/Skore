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

#include "LightComponent.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderStorage.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{

	void LightComponent::Create(ComponentSettings& settings)
	{
		m_renderStorage = GetScene()->GetRenderStorage();
		m_renderStorage->RegisterLightProxy(this);
		m_renderStorage->SetLightTransform(this, entity->GetGlobalTransform());
		m_renderStorage->SetLightType(this, m_lightType);
		m_renderStorage->SetLightColor(this, m_color);
		m_renderStorage->SetLightIntensity(this, m_intensity);
		m_renderStorage->SetLightRange(this, m_range);
		m_renderStorage->SetLightInnerConeAngle(this, m_innerConeAngle);
		m_renderStorage->SetLightOuterConeAngle(this, m_outerConeAngle);
	}

	void LightComponent::Destroy()
	{
		if (!m_renderStorage) return;

		m_renderStorage->RemoveLightProxy(this);
	}

	void LightComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_renderStorage) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_renderStorage->SetLightVisible(this, true);
				break;
			case EntityEventType::EntityDeactivated:
				m_renderStorage->SetLightVisible(this, false);
				break;
			case EntityEventType::TransformUpdated:
				m_renderStorage->SetLightTransform(this, entity->GetGlobalTransform());
				break;
		}
	}

	void LightComponent::SetLightType(LightType type)
	{
		m_lightType = type;

		if (m_renderStorage)
		{
			m_renderStorage->SetLightType(this, m_lightType);
		}

	}

	LightType LightComponent::GetLightType() const
	{
		return m_lightType;
	}

	void LightComponent::SetColor(const Color& color)
	{
		m_color = color;
		if (m_renderStorage)
		{
			m_renderStorage->SetLightColor(this, m_color);
		}
	}

	const Color& LightComponent::GetColor() const
	{
		return m_color;
	}

	void LightComponent::SetIntensity(f32 intensity)
	{
		m_intensity = intensity;
		if (m_renderStorage)
		{
			m_renderStorage->SetLightIntensity(this, m_intensity);
		}
	}

	f32 LightComponent::GetIntensity() const
	{
		return m_intensity;
	}

	void LightComponent::SetRange(f32 range)
	{
		m_range = range;
		if (m_renderStorage)
		{
			m_renderStorage->SetLightRange(this, m_range);
		}
	}

	f32 LightComponent::GetRange() const
	{
		return m_range;
	}

	void LightComponent::SetInnerConeAngle(f32 angle)
	{
		m_innerConeAngle = angle;

		if (m_renderStorage)
		{
			m_renderStorage->SetLightInnerConeAngle(this, m_innerConeAngle);
		}
	}

	f32 LightComponent::GetInnerConeAngle() const
	{
		return m_innerConeAngle;
	}

	void LightComponent::SetOuterConeAngle(f32 angle)
	{
		m_outerConeAngle = angle;
		if (m_renderStorage)
		{
			m_renderStorage->SetLightOuterConeAngle(this, m_outerConeAngle);
		}
	}

	f32 LightComponent::GetOuterConeAngle() const
	{
		return m_outerConeAngle;
	}

	void LightComponent::SetEnableShadows(bool enable)
	{
		m_enableShadows = enable;
		if (m_renderStorage)
		{
			m_renderStorage->SetLightEnableShadows(this, m_enableShadows);
		}
	}

	bool LightComponent::GetEnableShadows() const
	{
		return m_enableShadows;
	}

	void LightComponent::RegisterType(NativeReflectType<LightComponent>& type)
	{
		type.Field<&LightComponent::m_lightType, &LightComponent::GetLightType, &LightComponent::SetLightType>("lightType");
		type.Field<&LightComponent::m_color, &LightComponent::GetColor, &LightComponent::SetColor>("color");
		type.Field<&LightComponent::m_intensity, &LightComponent::GetIntensity, &LightComponent::SetIntensity>("intensity");
		type.Field<&LightComponent::m_range, &LightComponent::GetRange, &LightComponent::SetRange>("range");
		type.Field<&LightComponent::m_innerConeAngle, &LightComponent::GetInnerConeAngle, &LightComponent::SetInnerConeAngle>("innerConeAngle");
		type.Field<&LightComponent::m_outerConeAngle, &LightComponent::GetOuterConeAngle, &LightComponent::SetOuterConeAngle>("outerConeAngle");
		type.Field<&LightComponent::m_enableShadows, &LightComponent::GetEnableShadows, &LightComponent::SetEnableShadows>("enableShadows");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
	}
}
