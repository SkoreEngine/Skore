#include "Skore/Scene/Components/EnvironmentComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"

namespace Skore
{
	TypedRID<MaterialResource> EnvironmentComponent::GetSkyboxMaterial() const
	{
		return m_skyboxMaterial;
	}

	void EnvironmentComponent::SetSkyboxMaterial(TypedRID<MaterialResource> skyboxMaterial)
	{
		if (!m_materialCache && m_skyboxMaterial != skyboxMaterial)
		{
			m_materialCache = RenderResourceCache::GetMaterialCache(skyboxMaterial);
		}
		m_skyboxMaterial = skyboxMaterial;
	}

	bool EnvironmentComponent::GetUseSkyboxAsBackground() const
	{
		return m_useSkyboxAsBackground;
	}

	void EnvironmentComponent::SetUseSkyboxAsBackground(bool useSkyboxAsBackground)
	{
		m_useSkyboxAsBackground = useSkyboxAsBackground;
	}

	AmbientLightSource EnvironmentComponent::GetAmbientLightSource() const
	{
		return m_ambientLightSource;
	}

	void EnvironmentComponent::SetAmbientLightSource(AmbientLightSource source)
	{
		m_ambientLightSource = source;
	}

	Color EnvironmentComponent::GetAmbientLightColor() const
	{
		return m_ambientLightColor;
	}

	void EnvironmentComponent::SetAmbientLightColor(const Color& color)
	{
		m_ambientLightColor = color;
	}

	f32 EnvironmentComponent::GetAmbientLightIntensity() const
	{
		return m_ambientLightIntensity;
	}

	void EnvironmentComponent::SetAmbientLightIntensity(f32 intensity)
	{
		m_ambientLightIntensity = intensity;
	}

	ReflectedLightSource EnvironmentComponent::GetReflectedLightSource() const
	{
		return m_reflectedLightSource;
	}

	void EnvironmentComponent::SetReflectedLightSource(ReflectedLightSource source)
	{
		m_reflectedLightSource = source;
	}

	f32 EnvironmentComponent::GetReflectedLightIntensity() const
	{
		return m_reflectedLightIntensity;
	}

	void EnvironmentComponent::SetReflectedLightIntensity(f32 intensity)
	{
		m_reflectedLightIntensity = intensity;
	}

	MaterialResourceCache* EnvironmentComponent::GetMaterialCache() const
	{
		return m_materialCache.get();
	}

	void EnvironmentComponent::RegisterType(NativeReflectType<EnvironmentComponent>& type)
	{
		type.Field<&EnvironmentComponent::m_skyboxMaterial, &EnvironmentComponent::GetSkyboxMaterial, &EnvironmentComponent::SetSkyboxMaterial>("skyboxMaterial");
		type.Field<&EnvironmentComponent::m_useSkyboxAsBackground, &EnvironmentComponent::GetUseSkyboxAsBackground, &EnvironmentComponent::SetUseSkyboxAsBackground>("useSkyboxAsBackground");
		type.Field<&EnvironmentComponent::m_ambientLightSource, &EnvironmentComponent::GetAmbientLightSource, &EnvironmentComponent::SetAmbientLightSource>("ambientLightSource");
		type.Field<&EnvironmentComponent::m_ambientLightColor, &EnvironmentComponent::GetAmbientLightColor, &EnvironmentComponent::SetAmbientLightColor>("ambientLightColor");
		type.Field<&EnvironmentComponent::m_ambientLightIntensity, &EnvironmentComponent::GetAmbientLightIntensity, &EnvironmentComponent::SetAmbientLightIntensity>("ambientLightIntensity");
		type.Field<&EnvironmentComponent::m_reflectedLightSource, &EnvironmentComponent::GetReflectedLightSource, &EnvironmentComponent::SetReflectedLightSource>("reflectedLightSource");
		type.Field<&EnvironmentComponent::m_reflectedLightIntensity, &EnvironmentComponent::GetReflectedLightIntensity, &EnvironmentComponent::SetReflectedLightIntensity>("reflectedLightIntensity");
		type.Attribute<Iterable>();
	}
}
