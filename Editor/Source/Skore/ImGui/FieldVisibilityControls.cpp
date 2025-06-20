// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "ImGui.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/World/Components/LightComponent.hpp"

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
	}
}
