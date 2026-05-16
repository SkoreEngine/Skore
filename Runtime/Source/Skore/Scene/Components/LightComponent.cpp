#include "Skore/Scene/Components/LightComponent.hpp"

#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void LightComponent::Create()
	{
		lightObject = scene->renderObjects.CreateLightObject();
		lightObject->SetType(m_lightType);
		lightObject->SetColor(m_color);
		lightObject->SetEnableShadows(m_enableShadows);
		lightObject->SetIntensity(m_intensity);
		lightObject->SetRange(m_range);
		lightObject->SetInnerConeAngle(m_innerConeAngle);
		lightObject->SetOuterConeAngle(m_outerConeAngle);
		lightObject->SetTransform(entity->GetWorldTransform());
		lightObject->SetUserData(PtrToInt(entity));
		lightObject->SetCullingMask(m_cullingMask);
		lightObject->SetSourceRadius(m_sourceRadius);
	}

	void LightComponent::Destroy()
	{
		if (lightObject)
		{
			lightObject->Destroy();
		}
	}

	void LightComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (!lightObject) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				lightObject->SetVisible(true);
				break;
			case EntityEventType::EntityDeactivated:
				lightObject->SetVisible(false);
				break;
			case EntityEventType::TransformUpdated:
				lightObject->SetTransform(entity->GetWorldTransform());
				break;
		}
	}

	void LightComponent::SetLightType(LightType type)
	{
		m_lightType = type;
		if (lightObject) lightObject->SetType(type);
	}

	LightType LightComponent::GetLightType() const
	{
		return m_lightType;
	}

	void LightComponent::SetColor(const Color& color)
	{
		m_color = color;
		if (lightObject) lightObject->SetColor(color);
	}

	const Color& LightComponent::GetColor() const
	{
		return m_color;
	}

	void LightComponent::SetIntensity(f32 intensity)
	{
		m_intensity = intensity;
		if (lightObject) lightObject->SetIntensity(intensity);
	}

	f32 LightComponent::GetIntensity() const
	{
		return m_intensity;
	}

	void LightComponent::SetRange(f32 range)
	{
		m_range = range;
		if (lightObject) lightObject->SetRange(range);
	}

	f32 LightComponent::GetRange() const
	{
		return m_range;
	}

	void LightComponent::SetInnerConeAngle(f32 angle)
	{
		m_innerConeAngle = angle;
		if (lightObject) lightObject->SetInnerConeAngle(angle);
	}

	f32 LightComponent::GetInnerConeAngle() const
	{
		return m_innerConeAngle;
	}

	void LightComponent::SetOuterConeAngle(f32 angle)
	{
		m_outerConeAngle = angle;
		if (lightObject) lightObject->SetOuterConeAngle(angle);
	}

	f32 LightComponent::GetOuterConeAngle() const
	{
		return m_outerConeAngle;
	}

	void LightComponent::SetEnableShadows(bool enable)
	{
		m_enableShadows = enable;
		if (lightObject) lightObject->SetEnableShadows(enable);
	}

	bool LightComponent::GetEnableShadows() const
	{
		return m_enableShadows;
	}

	void LightComponent::SetCullingMask(u64 cullingMask)
	{
		m_cullingMask = cullingMask;
		if (lightObject) lightObject->SetCullingMask(cullingMask);
	}

	u64 LightComponent::GetCullingMask() const
	{
		return m_cullingMask;
	}

	void LightComponent::SetSourceRadius(f32 radius)
	{
		m_sourceRadius = radius;
		if (lightObject) lightObject->SetSourceRadius(radius);
	}

	f32 LightComponent::GetSourceRadius() const
	{
		return m_sourceRadius;
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
		type.Field<&LightComponent::m_cullingMask, &LightComponent::GetCullingMask, &LightComponent::SetCullingMask>("cullingMask").Attribute<UILayerMaskProperty>();
		type.Field<&LightComponent::m_sourceRadius, &LightComponent::GetSourceRadius, &LightComponent::SetSourceRadius>("sourceRadius");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
	}
}