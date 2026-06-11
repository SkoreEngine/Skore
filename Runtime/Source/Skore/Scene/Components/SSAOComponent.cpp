#include "Skore/Scene/Components/SSAOComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	f32 SSAOComponent::GetRadius() const
	{
		return m_radius;
	}

	void SSAOComponent::SetRadius(f32 radius)
	{
		m_radius = radius;
	}

	f32 SSAOComponent::GetFalloffRange() const
	{
		return m_falloffRange;
	}

	void SSAOComponent::SetFalloffRange(f32 falloffRange)
	{
		m_falloffRange = falloffRange;
	}

	f32 SSAOComponent::GetFinalValuePower() const
	{
		return m_finalValuePower;
	}

	void SSAOComponent::SetFinalValuePower(f32 finalValuePower)
	{
		m_finalValuePower = finalValuePower;
	}

	i32 SSAOComponent::GetDenoisePasses() const
	{
		return m_denoisePasses;
	}

	void SSAOComponent::SetDenoisePasses(i32 denoisePasses)
	{
		m_denoisePasses = denoisePasses;
	}

	void SSAOComponent::RegisterType(NativeReflectType<SSAOComponent>& type)
	{
		type.Field<&SSAOComponent::m_radius, &SSAOComponent::GetRadius, &SSAOComponent::SetRadius>("radius");
		type.Field<&SSAOComponent::m_falloffRange, &SSAOComponent::GetFalloffRange, &SSAOComponent::SetFalloffRange>("falloffRange");
		type.Field<&SSAOComponent::m_finalValuePower, &SSAOComponent::GetFinalValuePower, &SSAOComponent::SetFinalValuePower>("finalValuePower");
		type.Field<&SSAOComponent::m_denoisePasses, &SSAOComponent::GetDenoisePasses, &SSAOComponent::SetDenoisePasses>("denoisePasses");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false, .category = "Rendering"});
		type.Attribute<Iterable>();
	}
}
