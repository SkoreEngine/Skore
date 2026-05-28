#pragma once
#include "Skore/Core/Color.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	class SK_API LightComponent : public Component
	{
	public:
		SK_CLASS(LightComponent, Component);

		void      SetLightType(LightType type);
		LightType GetLightType() const;

		void         SetColor(const Color& color);
		const Color& GetColor() const;

		void SetIntensity(f32 intensity);
		f32  GetIntensity() const;

		void SetRange(f32 range);
		f32  GetRange() const;

		// Spot light specific properties (degrees)
		void SetInnerConeAngle(f32 angle);
		f32  GetInnerConeAngle() const;

		void SetOuterConeAngle(f32 angle);
		f32  GetOuterConeAngle() const;

		void SetEnableShadows(bool enable);
		bool GetEnableShadows() const;

		void SetMaxShadowDistance(f32 distance);
		f32  GetMaxShadowDistance() const;

		void SetSplitLambda(f32 lambda);
		f32  GetSplitLambda() const;

		void SetInterleavedCascadeUpdates(bool enable);
		bool GetInterleavedCascadeUpdates() const;

		void SetCullingMask(u64 cullingMask);
		u64  GetCullingMask() const;

		void SetSourceRadius(f32 radius);
		f32  GetSourceRadius() const;

		// radians as expected by the render pipelines (sign-flipped to preserve original behavior)
		f32 GetInnerConeAngleRadians() const;
		f32 GetOuterConeAngleRadians() const;

		static void RegisterType(NativeReflectType<LightComponent>& type);

	private:
		LightType m_lightType = LightType::Directional;
		Color     m_color = Color::WHITE;
		f32       m_intensity = 1.0f;
		f32       m_range = 10.0f;
		f32       m_innerConeAngle = 25.0f;
		f32       m_outerConeAngle = 30.0f;
		bool      m_enableShadows = true;
		u64       m_cullingMask = ~0ULL;
		f32       m_sourceRadius = 0.0f;
		f32       m_maxShadowDistance = 100.0f;
		f32       m_splitLambda = 0.85f;
		bool      m_interleavedCascadeUpdates = true;
	};
}
