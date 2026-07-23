
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"

namespace Skore
{

	template<auto ...LightTypes>
	bool ResourceLightTypeCheck(const ResourceObject& resourceObject)
	{
		LightType type = resourceObject.GetEnum<LightType>(resourceObject.GetIndex("lightType"));
		auto res = ((type == LightTypes) || ...);
		return res;
	}

	template <auto Value>
	bool AnimationParameterTypeCheck(const ResourceObject& resourceObject)
	{
		return resourceObject.GetEnum<AnimationParameterType>(AnimationParameterResource::Type) == Value;
	}

	void RegisterFieldVisibilityControls()
	{
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<LightComponent>::ID(), "range", ResourceLightTypeCheck<LightType::Point, LightType::Spot>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<LightComponent>::ID(), "innerConeAngle", ResourceLightTypeCheck<LightType::Spot>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<LightComponent>::ID(), "outerConeAngle", ResourceLightTypeCheck<LightType::Spot>);

		ImGuiRegisterResourceFieldVisibilityControl(sktypeid(AnimationParameterResource), "FloatValue", AnimationParameterTypeCheck<AnimationParameterType::Float>);
		ImGuiRegisterResourceFieldVisibilityControl(sktypeid(AnimationParameterResource), "IntValue", AnimationParameterTypeCheck<AnimationParameterType::Int>);
		ImGuiRegisterResourceFieldVisibilityControl(sktypeid(AnimationParameterResource), "BoolValue", AnimationParameterTypeCheck<AnimationParameterType::Bool>);
	}
}
