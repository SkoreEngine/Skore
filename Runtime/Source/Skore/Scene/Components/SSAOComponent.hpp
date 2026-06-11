#pragma once
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	//Enables screen-space ambient occlusion (XeGTAO) on the default render pipeline.
	//Adding this component to the scene enables the XeGTAORenderModule.
	class SK_API SSAOComponent : public Component
	{
	public:
		SK_CLASS(SSAOComponent, Component);

		f32  GetRadius() const;
		void SetRadius(f32 radius);

		f32  GetFalloffRange() const;
		void SetFalloffRange(f32 falloffRange);

		f32  GetFinalValuePower() const;
		void SetFinalValuePower(f32 finalValuePower);

		i32  GetDenoisePasses() const;
		void SetDenoisePasses(i32 denoisePasses);

		static void RegisterType(NativeReflectType<SSAOComponent>& type);

	private:
		f32 m_radius = 0.3f;          // world (view) space size of the occlusion sphere
		f32 m_falloffRange = 0.615f;  // distant samples contribute less
		f32 m_finalValuePower = 2.2f; // power curve applied to the final occlusion value
		i32 m_denoisePasses = 1;      // 0: disabled; 1: sharp; 2: medium; 3: soft
	};
}
