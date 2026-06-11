#include "Skore/Scene/Components/BloomComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	f32 BloomComponent::GetThreshold() const
	{
		return m_threshold;
	}

	void BloomComponent::SetThreshold(f32 threshold)
	{
		m_threshold = threshold;
	}

	f32 BloomComponent::GetSoftKnee() const
	{
		return m_softKnee;
	}

	void BloomComponent::SetSoftKnee(f32 softKnee)
	{
		m_softKnee = softKnee;
	}

	f32 BloomComponent::GetRadius() const
	{
		return m_radius;
	}

	void BloomComponent::SetRadius(f32 radius)
	{
		m_radius = radius;
	}

	f32 BloomComponent::GetIntensity() const
	{
		return m_intensity;
	}

	void BloomComponent::SetIntensity(f32 intensity)
	{
		m_intensity = intensity;
	}

	void BloomComponent::RegisterType(NativeReflectType<BloomComponent>& type)
	{
		type.Field<&BloomComponent::m_threshold, &BloomComponent::GetThreshold, &BloomComponent::SetThreshold>("threshold");
		type.Field<&BloomComponent::m_softKnee, &BloomComponent::GetSoftKnee, &BloomComponent::SetSoftKnee>("softKnee");
		type.Field<&BloomComponent::m_radius, &BloomComponent::GetRadius, &BloomComponent::SetRadius>("radius");
		type.Field<&BloomComponent::m_intensity, &BloomComponent::GetIntensity, &BloomComponent::SetIntensity>("intensity");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false, .category = "Rendering"});
		type.Attribute<Iterable>();
	}
}
