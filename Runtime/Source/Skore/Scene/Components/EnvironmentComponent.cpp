#include "Skore/Scene/Components/EnvironmentComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	TypedRID<TextureResource> EnvironmentComponent::GetPanoramicTexture() const
	{
		return m_panoramicTexture;
	}

	void EnvironmentComponent::SetPanoramicTexture(TypedRID<TextureResource> panoramicTexture)
	{
		if (m_panoramicTexture != panoramicTexture || !m_environmentCache)
		{
			m_environmentCache = panoramicTexture ? RenderResourceCache::GetEnvironmentCache(panoramicTexture, false) : nullptr;
		}
		m_panoramicTexture = panoramicTexture;
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

	EnvironmentResourceCache* EnvironmentComponent::GetEnvironmentCache() const
	{
		return m_environmentCache.get();
	}

	void EnvironmentComponent::RegisterType(NativeReflectType<EnvironmentComponent>& type)
	{
		type.Field<&EnvironmentComponent::m_panoramicTexture, &EnvironmentComponent::GetPanoramicTexture, &EnvironmentComponent::SetPanoramicTexture>("panoramicTexture");
		type.Field<&EnvironmentComponent::m_useSkyboxAsBackground, &EnvironmentComponent::GetUseSkyboxAsBackground, &EnvironmentComponent::SetUseSkyboxAsBackground>("useSkyboxAsBackground");
		type.Field<&EnvironmentComponent::m_ambientLightSource, &EnvironmentComponent::GetAmbientLightSource, &EnvironmentComponent::SetAmbientLightSource>("ambientLightSource");
		type.Field<&EnvironmentComponent::m_ambientLightColor, &EnvironmentComponent::GetAmbientLightColor, &EnvironmentComponent::SetAmbientLightColor>("ambientLightColor");
		type.Field<&EnvironmentComponent::m_ambientLightIntensity, &EnvironmentComponent::GetAmbientLightIntensity, &EnvironmentComponent::SetAmbientLightIntensity>("ambientLightIntensity");
		type.Field<&EnvironmentComponent::m_reflectedLightSource, &EnvironmentComponent::GetReflectedLightSource, &EnvironmentComponent::SetReflectedLightSource>("reflectedLightSource");
		type.Field<&EnvironmentComponent::m_reflectedLightIntensity, &EnvironmentComponent::GetReflectedLightIntensity, &EnvironmentComponent::SetReflectedLightIntensity>("reflectedLightIntensity");
		type.Attribute<Iterable>();
	}
}
