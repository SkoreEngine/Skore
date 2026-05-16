
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

	template <auto MaterialType>
	bool MaterialTypeCheck(const ResourceObject& resourceObject)
	{
		MaterialResource::MaterialType type = resourceObject.GetEnum<MaterialResource::MaterialType>(MaterialResource::Type);
		return type == MaterialType;
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

		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "BaseColor", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "BaseColorTexture", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "NormalTexture", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "NormalMultiplier", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "Metallic", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "MetallicTexture", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "MetallicTextureChannel", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "Roughness", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "RoughnessTexture", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "RoughnessTextureChannel", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "EmissiveColor", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "EmissiveFactor", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "EmissiveTexture", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "OcclusionTexture", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "OcclusionStrength", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "OcclusionTextureChannel", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "AlphaCutoff", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "AlphaMode", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "UvScale", MaterialTypeCheck<MaterialResource::MaterialType::Opaque>);

		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "SphericalTexture", MaterialTypeCheck<MaterialResource::MaterialType::SkyboxEquirectangular>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "Exposure", MaterialTypeCheck<MaterialResource::MaterialType::SkyboxEquirectangular>);
		ImGuiRegisterResourceFieldVisibilityControl(TypeInfo<MaterialResource>::ID(), "BackgroundColor", MaterialTypeCheck<MaterialResource::MaterialType::SkyboxEquirectangular>);

		ImGuiRegisterResourceFieldVisibilityControl(sktypeid(AnimationParameterResource), "FloatValue", AnimationParameterTypeCheck<AnimationParameterType::Float>);
		ImGuiRegisterResourceFieldVisibilityControl(sktypeid(AnimationParameterResource), "IntValue", AnimationParameterTypeCheck<AnimationParameterType::Int>);
		ImGuiRegisterResourceFieldVisibilityControl(sktypeid(AnimationParameterResource), "BoolValue", AnimationParameterTypeCheck<AnimationParameterType::Bool>);
	}
}
