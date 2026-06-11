#pragma once
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	//Enables the bloom post-process on the default render pipeline.
	//Adding this component to the scene enables the BloomModule.
	class SK_API BloomComponent : public Component
	{
	public:
		SK_CLASS(BloomComponent, Component);

		f32  GetThreshold() const;
		void SetThreshold(f32 threshold);

		f32  GetSoftKnee() const;
		void SetSoftKnee(f32 softKnee);

		f32  GetRadius() const;
		void SetRadius(f32 radius);

		f32  GetIntensity() const;
		void SetIntensity(f32 intensity);

		static void RegisterType(NativeReflectType<BloomComponent>& type);

	private:
		f32 m_threshold = 1.0f;  // luminance above which pixels start to bloom
		f32 m_softKnee = 0.5f;   // soft threshold knee (0: hard cutoff, 1: smooth)
		f32 m_radius = 1.0f;     // upsample filter radius (spread of the bloom)
		f32 m_intensity = 0.04f; // how strongly bloom is blended into the final image
	};
}
