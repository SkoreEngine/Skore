#include "Skore/Scene/Components/LightComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/DebugDraw.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void LightComponent::SetLightType(LightType type)
	{
		m_lightType = type;
	}

	LightType LightComponent::GetLightType() const
	{
		return m_lightType;
	}

	void LightComponent::SetColor(const Color& color)
	{
		m_color = color;
	}

	const Color& LightComponent::GetColor() const
	{
		return m_color;
	}

	void LightComponent::SetIntensity(f32 intensity)
	{
		m_intensity = intensity;
	}

	f32 LightComponent::GetIntensity() const
	{
		return m_intensity;
	}

	void LightComponent::SetRange(f32 range)
	{
		m_range = range;
	}

	f32 LightComponent::GetRange() const
	{
		return m_range;
	}

	void LightComponent::SetInnerConeAngle(f32 angle)
	{
		m_innerConeAngle = angle;
	}

	f32 LightComponent::GetInnerConeAngle() const
	{
		return m_innerConeAngle;
	}

	void LightComponent::SetOuterConeAngle(f32 angle)
	{
		m_outerConeAngle = angle;
	}

	f32 LightComponent::GetOuterConeAngle() const
	{
		return m_outerConeAngle;
	}

	void LightComponent::SetEnableShadows(bool enable)
	{
		m_enableShadows = enable;
	}

	bool LightComponent::GetEnableShadows() const
	{
		return m_enableShadows;
	}

	void LightComponent::SetMaxShadowDistance(f32 distance)
	{
		m_maxShadowDistance = distance;
	}

	f32 LightComponent::GetMaxShadowDistance() const
	{
		return m_maxShadowDistance;
	}

	void LightComponent::SetSplitLambda(f32 lambda)
	{
		m_splitLambda = lambda;
	}

	f32 LightComponent::GetSplitLambda() const
	{
		return m_splitLambda;
	}

	void LightComponent::SetInterleavedCascadeUpdates(bool enable)
	{
		m_interleavedCascadeUpdates = enable;
	}

	bool LightComponent::GetInterleavedCascadeUpdates() const
	{
		return m_interleavedCascadeUpdates;
	}

	void LightComponent::SetOpaqueShadowCascades(u32 count)
	{
		m_opaqueShadowCascades = count;
	}

	u32 LightComponent::GetOpaqueShadowCascades() const
	{
		return m_opaqueShadowCascades;
	}

	void LightComponent::SetCullingMask(u64 cullingMask)
	{
		m_cullingMask = cullingMask;
	}

	u64 LightComponent::GetCullingMask() const
	{
		return m_cullingMask;
	}

	void LightComponent::SetSourceRadius(f32 radius)
	{
		m_sourceRadius = radius;
	}

	f32 LightComponent::GetSourceRadius() const
	{
		return m_sourceRadius;
	}

	f32 LightComponent::GetInnerConeAngleRadians() const
	{
		return Math::Radians(-m_innerConeAngle);
	}

	f32 LightComponent::GetOuterConeAngleRadians() const
	{
		return Math::Radians(-m_outerConeAngle);
	}

	void LightComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::DrawGizmos)
		{
			DrawGizmosEvent* data = static_cast<DrawGizmosEvent*>(event.eventData);
			DebugDraw* debugDraw = data->debugDraw;

			const Mat4& transform = entity->GetWorldTransform();
			Vec3 position = Mat4::GetTranslation(transform);
			Vec3 direction = Vec3::Normalize(Mat4::GetForwardVector(transform));

			Color gizmoColor = Color::YELLOW;

			switch (m_lightType)
			{
				case LightType::Directional:
				{
					constexpr f32 rayLength = 2.0f;
					constexpr f32 diskRadius = 0.25f;

					Vec3 tangent, bitangent;
					DebugDraw::BuildOrthonormalBasis(direction, tangent, bitangent);

					debugDraw->DrawCircle(position, diskRadius, direction, 1.0f, gizmoColor, true, 16);
					debugDraw->DrawArrow(position, position + direction * rayLength, 1.0f, gizmoColor);

					Vec3 rayOffsets[4] = {
						tangent * diskRadius,
						tangent * -diskRadius,
						bitangent * diskRadius,
						bitangent * -diskRadius
					};

					for (const Vec3& offset : rayOffsets)
					{
						debugDraw->DrawLine(position + offset, position + offset + direction * (rayLength * 0.75f), 1.0f, gizmoColor);
					}
					break;
				}
				case LightType::Point:
				{
					debugDraw->DrawSphere(position, m_range, 1.0f, gizmoColor);

					if (m_sourceRadius > 0.0f)
					{
						debugDraw->DrawSphere(position, m_sourceRadius, 1.0f, Color::WHITE);
					}
					break;
				}
				case LightType::Spot:
				{
					debugDraw->DrawCone(position, direction, m_range, Math::Radians(Math::Abs(m_outerConeAngle)), 1.0f, gizmoColor);

					if (m_innerConeAngle < m_outerConeAngle)
					{
						debugDraw->DrawCone(position, direction, m_range, Math::Radians(Math::Abs(m_innerConeAngle)), 1.0f, Color{255, 255, 0, 90});
					}
					break;
				}
			}
		}
	}

	void LightComponent::RegisterType(NativeReflectType<LightComponent>& type)
	{
		type.Field<&LightComponent::m_lightType, &LightComponent::GetLightType, &LightComponent::SetLightType>("lightType");
		type.Field<&LightComponent::m_color, &LightComponent::GetColor, &LightComponent::SetColor>("color");
		type.Field<&LightComponent::m_intensity, &LightComponent::GetIntensity, &LightComponent::SetIntensity>("intensity");
		type.Field<&LightComponent::m_range, &LightComponent::GetRange, &LightComponent::SetRange>("range");
		type.Field<&LightComponent::m_innerConeAngle, &LightComponent::GetInnerConeAngle, &LightComponent::SetInnerConeAngle>("innerConeAngle");
		type.Field<&LightComponent::m_outerConeAngle, &LightComponent::GetOuterConeAngle, &LightComponent::SetOuterConeAngle>("outerConeAngle");
		type.Field<&LightComponent::m_enableShadows, &LightComponent::GetEnableShadows, &LightComponent::SetEnableShadows>("enableShadows");
		type.Field<&LightComponent::m_maxShadowDistance, &LightComponent::GetMaxShadowDistance, &LightComponent::SetMaxShadowDistance>("maxShadowDistance");
		type.Field<&LightComponent::m_splitLambda, &LightComponent::GetSplitLambda, &LightComponent::SetSplitLambda>("splitLambda");
		type.Field<&LightComponent::m_interleavedCascadeUpdates, &LightComponent::GetInterleavedCascadeUpdates, &LightComponent::SetInterleavedCascadeUpdates>("interleavedCascadeUpdates");
		type.Field<&LightComponent::m_opaqueShadowCascades, &LightComponent::GetOpaqueShadowCascades, &LightComponent::SetOpaqueShadowCascades>("opaqueShadowCascades");
		type.Field<&LightComponent::m_cullingMask, &LightComponent::GetCullingMask, &LightComponent::SetCullingMask>("cullingMask").Attribute<UILayerMaskProperty>();
		type.Field<&LightComponent::m_sourceRadius, &LightComponent::GetSourceRadius, &LightComponent::SetSourceRadius>("sourceRadius");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
		type.Attribute<Iterable>();
	}
}
