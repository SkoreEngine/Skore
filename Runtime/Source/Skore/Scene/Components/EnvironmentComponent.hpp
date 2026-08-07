#pragma once
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	class SK_API EnvironmentComponent : public Component
	{
	public:
		SK_CLASS(EnvironmentComponent, Component);

		// Skybox getters/setters
		TypedRID<TextureResource> GetPanoramicTexture() const;
		void                      SetPanoramicTexture(TypedRID<TextureResource> panoramicTexture);
		bool GetUseSkyboxAsBackground() const;
		void SetUseSkyboxAsBackground(bool use);

		// Ambient light getters/setters
		AmbientLightSource GetAmbientLightSource() const;
		void               SetAmbientLightSource(AmbientLightSource source);
		Color              GetAmbientLightColor() const;
		void               SetAmbientLightColor(const Color& color);
		f32                GetAmbientLightIntensity() const;
		void               SetAmbientLightIntensity(f32 intensity);

		// Reflected light getters/setters
		ReflectedLightSource GetReflectedLightSource() const;
		void                 SetReflectedLightSource(ReflectedLightSource source);
		f32                  GetReflectedLightIntensity() const;
		void                 SetReflectedLightIntensity(f32 intensity);

		EnvironmentResourceCache* GetEnvironmentCache() const;

		static void RegisterType(NativeReflectType<EnvironmentComponent>& type);

	private:
		TypedRID<TextureResource> m_panoramicTexture = {};
		bool                      m_useSkyboxAsBackground = true;

		AmbientLightSource m_ambientLightSource = AmbientLightSource::Skybox;
		Color              m_ambientLightColor = Color::WHITE;
		f32                m_ambientLightIntensity = 1.0f;

		ReflectedLightSource m_reflectedLightSource = ReflectedLightSource::Skybox;
		f32                  m_reflectedLightIntensity = 1.0f;

		EnvironmentResourceCachePtr m_environmentCache;
	};
}
