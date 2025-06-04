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


#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"
#include "Skore/ImGui/ImGui.hpp"

namespace Skore
{
	struct MaterialHandler : AssetHandler
	{
		SK_CLASS(MaterialHandler, AssetHandler);

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<MaterialAsset>::ID();
		}

		void OpenAsset(AssetFileOld* assetFile) override
		{
			Editor::GetCurrentWorkspace().OpenAsset(assetFile);
		}

		String Extension() override
		{
			return ".material";
		}

		String Name() override
		{
			return "Material";
		}
	};

	bool MaterialCheckOpaque(Object* object)
	{
		if (MaterialAsset* materialAsset = object->SafeCast<MaterialAsset>())
		{
			return materialAsset->type == MaterialAsset::MaterialType::Opaque;
		}
		return false;
	}

	bool MaterialCheckSkybox(Object* object)
	{
		if (MaterialAsset* materialAsset = object->SafeCast<MaterialAsset>())
		{
			return materialAsset->type == MaterialAsset::MaterialType::SkyboxEquirectangular;
		}
		return false;
	}


	void RegisterMaterialAssetHandler()
	{
		Reflection::Type<MaterialHandler>();

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "baseColor", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "baseColorTexture", MaterialCheckOpaque);

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "normalTexture", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "normalMultiplier", MaterialCheckOpaque);

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "metallic", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "metallicTexture", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "metallicTextureChannel", MaterialCheckOpaque);

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "roughness", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "roughnessTexture", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "roughnessTextureChannel", MaterialCheckOpaque);

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "emissiveFactor", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "emissiveTexture", MaterialCheckOpaque);

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "occlusionTexture", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "occlusionStrength", MaterialCheckOpaque);

		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "alphaCutoff", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "alphaMode", MaterialCheckOpaque);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "uvScale", MaterialCheckOpaque);


		//sky material
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "sphericalTexture", MaterialCheckSkybox);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "exposure", MaterialCheckSkybox);
		ImGuiRegisterFieldVisibilityControl(TypeInfo<MaterialAsset>::ID(), "backgroundColor", MaterialCheckSkybox);
	}
}
