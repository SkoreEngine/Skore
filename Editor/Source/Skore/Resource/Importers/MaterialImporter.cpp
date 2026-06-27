#include "Skore/Resource/Importers/MaterialImporter.hpp"

#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	RID ImportMaterial(const MaterialImportData& material, const SubResourceAllocator& alloc, StringView subId)
	{
		UndoRedoScope* scope = alloc.scope;
		RID materialResource = alloc.Create<MaterialResource>(subId);

		ResourceObject materialObject = Resources::Write(materialResource);
		materialObject.SetString(MaterialResource::Name, material.name.Empty() ? StringView("Material") : material.name);

		if (material.hasBaseColor)
		{
			materialObject.SetColor(MaterialResource::BaseColor, material.baseColor);
		}

		if (material.hasEmissiveColor)
		{
			materialObject.SetColor(MaterialResource::EmissiveColor, material.emissiveColor);
		}

		if (material.hasMetallic)
		{
			materialObject.SetFloat(MaterialResource::Metallic, material.metallic);
		}

		if (material.hasRoughness)
		{
			materialObject.SetFloat(MaterialResource::Roughness, material.roughness);
		}

		if (material.hasNormalMultiplier)
		{
			materialObject.SetFloat(MaterialResource::NormalMultiplier, material.normalMultiplier);
		}

		if (material.hasEmissiveFactor)
		{
			materialObject.SetFloat(MaterialResource::EmissiveFactor, material.emissiveFactor);
		}

		if (material.hasOcclusionStrength)
		{
			materialObject.SetFloat(MaterialResource::OcclusionStrength, material.occlusionStrength);
		}

		if (material.hasAlphaCutoff)
		{
			materialObject.SetFloat(MaterialResource::AlphaCutoff, material.alphaCutoff);
		}

		if (material.hasAlphaMode)
		{
			materialObject.SetEnum(MaterialResource::AlphaMode, material.alphaMode);
		}

		if (material.baseColorTexture)
		{
			materialObject.SetReference(MaterialResource::BaseColorTexture, material.baseColorTexture);
		}

		if (material.normalTexture)
		{
			materialObject.SetReference(MaterialResource::NormalTexture, material.normalTexture);
		}

		if (material.metallicTexture)
		{
			materialObject.SetReference(MaterialResource::MetallicTexture, material.metallicTexture);
		}

		if (material.roughnessTexture)
		{
			materialObject.SetReference(MaterialResource::RoughnessTexture, material.roughnessTexture);
		}

		if (material.emissiveTexture)
		{
			materialObject.SetReference(MaterialResource::EmissiveTexture, material.emissiveTexture);
		}

		if (material.occlusionTexture)
		{
			materialObject.SetReference(MaterialResource::OcclusionTexture, material.occlusionTexture);
		}

		if (material.hasMetallicTextureChannel)
		{
			materialObject.SetEnum(MaterialResource::MetallicTextureChannel, material.metallicTextureChannel);
		}

		if (material.hasRoughnessTextureChannel)
		{
			materialObject.SetEnum(MaterialResource::RoughnessTextureChannel, material.roughnessTextureChannel);
		}

		if (material.hasOcclusionTextureChannel)
		{
			materialObject.SetEnum(MaterialResource::OcclusionTextureChannel, material.occlusionTextureChannel);
		}

		materialObject.Commit(scope);

		return materialResource;
	}
}
