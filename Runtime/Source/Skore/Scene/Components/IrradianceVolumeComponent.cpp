#include "Skore/Scene/Components/IrradianceVolumeComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/DebugDraw.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void IrradianceVolumeComponent::SetRaysPerProbe(i32 raysPerProbe)
	{
		m_raysPerProbe = raysPerProbe;
	}

	i32 IrradianceVolumeComponent::GetRaysPerProbe() const
	{
		return m_raysPerProbe;
	}

	void IrradianceVolumeComponent::SetCascadeCount(i32 cascadeCount)
	{
		m_cascadeCount = cascadeCount;
	}

	i32 IrradianceVolumeComponent::GetCascadeCount() const
	{
		return m_cascadeCount;
	}

	void IrradianceVolumeComponent::SetProbeCountX(i32 probeCountX)
	{
		m_probeCountX = probeCountX;
	}

	i32 IrradianceVolumeComponent::GetProbeCountX() const
	{
		return m_probeCountX;
	}

	void IrradianceVolumeComponent::SetProbeCountY(i32 probeCountY)
	{
		m_probeCountY = probeCountY;
	}

	i32 IrradianceVolumeComponent::GetProbeCountY() const
	{
		return m_probeCountY;
	}

	void IrradianceVolumeComponent::SetProbeCountZ(i32 probeCountZ)
	{
		m_probeCountZ = probeCountZ;
	}

	i32 IrradianceVolumeComponent::GetProbeCountZ() const
	{
		return m_probeCountZ;
	}

	void IrradianceVolumeComponent::SetProbeSpacing(f32 probeSpacing)
	{
		m_probeSpacing = probeSpacing;
	}

	f32 IrradianceVolumeComponent::GetProbeSpacing() const
	{
		return m_probeSpacing;
	}

	void IrradianceVolumeComponent::SetScrollWithCamera(bool scrollWithCamera)
	{
		m_scrollWithCamera = scrollWithCamera;
	}

	bool IrradianceVolumeComponent::GetScrollWithCamera() const
	{
		return m_scrollWithCamera;
	}

	void IrradianceVolumeComponent::SetHysteresis(f32 hysteresis)
	{
		m_hysteresis = hysteresis;
	}

	f32 IrradianceVolumeComponent::GetHysteresis() const
	{
		return m_hysteresis;
	}

	void IrradianceVolumeComponent::SetIndirectIntensity(f32 indirectIntensity)
	{
		m_indirectIntensity = indirectIntensity;
	}

	f32 IrradianceVolumeComponent::GetIndirectIntensity() const
	{
		return m_indirectIntensity;
	}

	void IrradianceVolumeComponent::SetProbeRelocation(bool enable)
	{
		m_probeRelocation = enable;
	}

	bool IrradianceVolumeComponent::GetProbeRelocation() const
	{
		return m_probeRelocation;
	}

	void IrradianceVolumeComponent::SetProbeClassification(bool enable)
	{
		m_probeClassification = enable;
	}

	bool IrradianceVolumeComponent::GetProbeClassification() const
	{
		return m_probeClassification;
	}

	u64 IrradianceVolumeComponent::GetResourceConfigVersion() const
	{
		u64 hash = 1469598103934665603ull;
		auto mix = [&](u64 value)
		{
			hash ^= value;
			hash *= 1099511628211ull;
		};
		mix(static_cast<u32>(m_raysPerProbe));
		mix(static_cast<u32>(m_cascadeCount));
		mix(static_cast<u32>(m_probeCountX));
		mix(static_cast<u32>(m_probeCountY));
		mix(static_cast<u32>(m_probeCountZ));
		mix(static_cast<u64>(m_probeSpacing * 1000.0f));
		return hash;
	}

	void IrradianceVolumeComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::DrawGizmos)
		{
			DrawGizmosEvent* data = static_cast<DrawGizmosEvent*>(event.eventData);

			Mat4 translation = Mat4::Translate(entity->GetWorldPosition());

			//each cascade doubles the probe spacing
			f32 spacing = m_probeSpacing;
			for (i32 cascade = 0; cascade < m_cascadeCount; ++cascade)
			{
				Vec3 halfExtents = Vec3((f32)m_probeCountX, (f32)m_probeCountY, (f32)m_probeCountZ) * spacing * 0.5f;
				u8 alpha = static_cast<u8>(Math::Max(40, 255 - cascade * 70));
				data->debugDraw->DrawBox(translation, halfExtents, 1.0f, Color{80, 220, 120, alpha});
				spacing *= 2.0f;
			}
		}
	}

	void IrradianceVolumeComponent::RegisterType(NativeReflectType<IrradianceVolumeComponent>& type)
	{
		type.Field<&IrradianceVolumeComponent::m_raysPerProbe, &IrradianceVolumeComponent::GetRaysPerProbe, &IrradianceVolumeComponent::SetRaysPerProbe>("raysPerProbe");
		type.Field<&IrradianceVolumeComponent::m_cascadeCount, &IrradianceVolumeComponent::GetCascadeCount, &IrradianceVolumeComponent::SetCascadeCount>("cascadeCount");
		type.Field<&IrradianceVolumeComponent::m_probeCountX, &IrradianceVolumeComponent::GetProbeCountX, &IrradianceVolumeComponent::SetProbeCountX>("probeCountX");
		type.Field<&IrradianceVolumeComponent::m_probeCountY, &IrradianceVolumeComponent::GetProbeCountY, &IrradianceVolumeComponent::SetProbeCountY>("probeCountY");
		type.Field<&IrradianceVolumeComponent::m_probeCountZ, &IrradianceVolumeComponent::GetProbeCountZ, &IrradianceVolumeComponent::SetProbeCountZ>("probeCountZ");
		type.Field<&IrradianceVolumeComponent::m_probeSpacing, &IrradianceVolumeComponent::GetProbeSpacing, &IrradianceVolumeComponent::SetProbeSpacing>("probeSpacing");
		type.Field<&IrradianceVolumeComponent::m_scrollWithCamera, &IrradianceVolumeComponent::GetScrollWithCamera, &IrradianceVolumeComponent::SetScrollWithCamera>("scrollWithCamera");
		type.Field<&IrradianceVolumeComponent::m_hysteresis, &IrradianceVolumeComponent::GetHysteresis, &IrradianceVolumeComponent::SetHysteresis>("hysteresis");
		type.Field<&IrradianceVolumeComponent::m_indirectIntensity, &IrradianceVolumeComponent::GetIndirectIntensity, &IrradianceVolumeComponent::SetIndirectIntensity>("indirectIntensity");
		type.Field<&IrradianceVolumeComponent::m_probeRelocation, &IrradianceVolumeComponent::GetProbeRelocation, &IrradianceVolumeComponent::SetProbeRelocation>("probeRelocation");
		type.Field<&IrradianceVolumeComponent::m_probeClassification, &IrradianceVolumeComponent::GetProbeClassification, &IrradianceVolumeComponent::SetProbeClassification>("probeClassification");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false, .category = "Rendering"});
		type.Attribute<Iterable>();
	}
}
