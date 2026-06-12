#pragma once
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	class SK_API IrradianceVolumeComponent : public Component
	{
	public:
		SK_CLASS(IrradianceVolumeComponent, Component);

		void SetRaysPerProbe(i32 raysPerProbe);
		i32  GetRaysPerProbe() const;

		void SetCascadeCount(i32 cascadeCount);
		i32  GetCascadeCount() const;

		void SetProbeCountX(i32 probeCountX);
		i32  GetProbeCountX() const;

		void SetProbeCountY(i32 probeCountY);
		i32  GetProbeCountY() const;

		void SetProbeCountZ(i32 probeCountZ);
		i32  GetProbeCountZ() const;

		void SetProbeSpacing(f32 probeSpacing);
		f32  GetProbeSpacing() const;

		void SetScrollWithCamera(bool scrollWithCamera);
		bool GetScrollWithCamera() const;

		void SetHysteresis(f32 hysteresis);
		f32  GetHysteresis() const;

		void SetIndirectIntensity(f32 indirectIntensity);
		f32  GetIndirectIntensity() const;

		void SetProbeRelocation(bool enable);
		bool GetProbeRelocation() const;

		void SetProbeClassification(bool enable);
		bool GetProbeClassification() const;

		u64 GetResourceConfigVersion() const;

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<IrradianceVolumeComponent>& type);

	private:
		i32  m_raysPerProbe = 144;
		i32  m_cascadeCount = 4;
		i32  m_probeCountX = 16;
		i32  m_probeCountY = 8;
		i32  m_probeCountZ = 16;
		f32  m_probeSpacing = 1.0f;
		bool m_scrollWithCamera = true;
		f32  m_hysteresis = 0.97f;
		f32  m_indirectIntensity = 2.0f;
		bool m_probeRelocation = true;
		bool m_probeClassification = true;
	};
}
