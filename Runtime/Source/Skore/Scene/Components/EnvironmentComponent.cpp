#include "Skore/Scene/Components/EnvironmentComponent.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	void EnvironmentComponent::Create(ComponentSettings& settings)
	{
		environmentObject = scene->renderObjects.CreateEnvironmentObject();
		environmentObject->SetMaterial(m_skyboxMaterial);
		environmentObject->SetUseAsSkybox(true);
		environmentObject->SetAmbientLightSource(m_ambientLightSource);
		environmentObject->SetAmbientLightColor(m_ambientLightColor);
		environmentObject->SetAmbientLightIntensity(m_ambientLightIntensity);
		environmentObject->SetReflectedLightSource(m_reflectedLightSource);
		environmentObject->SetReflectedLightIntensity(m_reflectedLightIntensity);
		
	}

	void EnvironmentComponent::Destroy()
	{
		if (environmentObject)
		{
			environmentObject->Destroy();
		}
	}

	void EnvironmentComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (!environmentObject) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				environmentObject->SetVisible(true);
				break;
			case EntityEventType::EntityDeactivated:
				environmentObject->SetVisible(false);
				break;
		}
	}

	TypedRID<MaterialResource> EnvironmentComponent::GetSkyboxMaterial() const
	{
		return m_skyboxMaterial;
	}

	void EnvironmentComponent::SetSkyboxMaterial(TypedRID<MaterialResource> skyboxMaterial)
	{
		m_skyboxMaterial = skyboxMaterial;
		if (environmentObject) environmentObject->SetMaterial(m_skyboxMaterial);
	}

	bool EnvironmentComponent::GetUseSkyboxAsBackground() const
	{
		return m_useSkyboxAsBackground;
	}

	void EnvironmentComponent::SetUseSkyboxAsBackground(bool useSkyboxAsBackground)
	{
		m_useSkyboxAsBackground = useSkyboxAsBackground;
		if (environmentObject) environmentObject->SetUseAsSkybox(useSkyboxAsBackground);
	}


	AmbientLightSource EnvironmentComponent::GetAmbientLightSource() const
	{
		return m_ambientLightSource;
	}

	void EnvironmentComponent::SetAmbientLightSource(AmbientLightSource source)
	{
		m_ambientLightSource = source;
		if (environmentObject) environmentObject->SetAmbientLightSource(source);
	}

	Color EnvironmentComponent::GetAmbientLightColor() const
	{
		return m_ambientLightColor;
	}

	void EnvironmentComponent::SetAmbientLightColor(const Color& color)
	{
		m_ambientLightColor = color;
		if (environmentObject) environmentObject->SetAmbientLightColor(color);
	}

	f32 EnvironmentComponent::GetAmbientLightIntensity() const
	{
		return m_ambientLightIntensity;
	}

	void EnvironmentComponent::SetAmbientLightIntensity(f32 intensity)
	{
		m_ambientLightIntensity = intensity;
		if (environmentObject) environmentObject->SetAmbientLightIntensity(intensity);
	}

	ReflectedLightSource EnvironmentComponent::GetReflectedLightSource() const
	{
		return m_reflectedLightSource;
	}

	void EnvironmentComponent::SetReflectedLightSource(ReflectedLightSource source)
	{
		m_reflectedLightSource = source;
		if (environmentObject) environmentObject->SetReflectedLightSource(source);
	}

	f32 EnvironmentComponent::GetReflectedLightIntensity() const
	{
		return m_reflectedLightIntensity;
	}

	void EnvironmentComponent::SetReflectedLightIntensity(f32 intensity)
	{
		m_reflectedLightIntensity = intensity;
		if (environmentObject) environmentObject->SetReflectedLightIntensity(intensity);
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
	}
}