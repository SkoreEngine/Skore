#include "Skore/Resource/Importers/MaterialImporter.hpp"

#include "Skore/Core/String.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"

// 1: import as a MaterialGraphResource instance of a base graph. 0: legacy standalone MaterialResource.
#ifndef SK_IMPORT_MATERIAL_AS_GRAPH_INSTANCE
	#define SK_IMPORT_MATERIAL_AS_GRAPH_INSTANCE 0
#endif

namespace Skore
{
	namespace
	{
		// One base graph per alpha mode (alpha mode is baked into the compiled shader, not overridable).
		constexpr const char* BaseMaterialOpaque = "Skore://MaterialGraphs/MaterialBase.matgraph";
		constexpr const char* BaseMaterialMasked = "Skore://MaterialGraphs/MaterialBase_Masked.matgraph";
		constexpr const char* BaseMaterialBlend  = "Skore://MaterialGraphs/MaterialBase_Blend.matgraph";
	}

	// Legacy: standalone MaterialResource.
	[[maybe_unused]] static RID ImportMaterialResource(const MaterialImportData& material, const SubResourceAllocator& alloc, StringView subId)
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

	// Instance of a base graph, overriding only the parameters exposed on it (named to match MaterialResource).
	[[maybe_unused]] static RID ImportMaterialGraphInstance(const MaterialImportData& material, const SubResourceAllocator& alloc, StringView subId)
	{
		UndoRedoScope* scope = alloc.scope;

		const char*                           parentPath = BaseMaterialOpaque;
		MaterialGraphResource::GraphAlphaMode graphAlpha = MaterialGraphResource::GraphAlphaMode::Opaque;
		if (material.hasAlphaMode)
		{
			switch (material.alphaMode)
			{
				case MaterialResource::MaterialAlphaMode::Mask:
					parentPath = BaseMaterialMasked;
					graphAlpha = MaterialGraphResource::GraphAlphaMode::Mask;
					break;
				case MaterialResource::MaterialAlphaMode::Blend:
					parentPath = BaseMaterialBlend;
					graphAlpha = MaterialGraphResource::GraphAlphaMode::Blend;
					break;
				default:
					break;
			}
		}

		RID  materialResource = alloc.Create<MaterialGraphResource>(subId);
		UUID base = Resources::GetUUID(materialResource);

		ResourceObject materialObject = Resources::Write(materialResource);
		materialObject.SetString(MaterialGraphResource::Name, material.name.Empty() ? StringView("Material") : material.name);
		materialObject.SetEnum(MaterialGraphResource::Kind, MaterialGraphResource::MaterialKind::Instance);
		materialObject.SetReference(MaterialGraphResource::Parent, Resources::FindByPath(parentPath));
		materialObject.SetEnum(MaterialGraphResource::AlphaMode, graphAlpha);
		if (material.hasAlphaCutoff)
		{
			materialObject.SetFloat(MaterialGraphResource::MaskCutoff, material.alphaCutoff);
		}

		// Sparse overrides keyed by parameter name; deterministic UUIDs keep reimports stable.
		auto addScalar = [&](StringView name, Vec4 value)
		{
			RID entry = Resources::Create<MaterialParameterOverrideResource>(SubResourceUUID(base, String{"param:"} + name), scope);
			ResourceObject entryObj = Resources::Write(entry);
			entryObj.SetString(MaterialParameterOverrideResource::ParameterName, name);
			entryObj.SetVec4(MaterialParameterOverrideResource::Value, value);
			entryObj.Commit(scope);
			materialObject.AddToSubObjectList(MaterialGraphResource::Parameters, entry);
		};

		auto addTexture = [&](StringView name, RID texture)
		{
			RID entry = Resources::Create<MaterialParameterOverrideResource>(SubResourceUUID(base, String{"param:"} + name), scope);
			ResourceObject entryObj = Resources::Write(entry);
			entryObj.SetString(MaterialParameterOverrideResource::ParameterName, name);
			entryObj.SetReference(MaterialParameterOverrideResource::Texture, texture);
			entryObj.Commit(scope);
			materialObject.AddToSubObjectList(MaterialGraphResource::Parameters, entry);
		};

		if (material.hasBaseColor)         addScalar("BaseColor", material.baseColor.ToVec4());
		if (material.baseColorTexture)     addTexture("BaseColorTexture", material.baseColorTexture);

		if (material.hasMetallic)          addScalar("Metallic", Vec4{material.metallic, 0.0f, 0.0f, 0.0f});
		if (material.metallicTexture)      addTexture("MetallicTexture", material.metallicTexture);

		if (material.hasRoughness)         addScalar("Roughness", Vec4{material.roughness, 0.0f, 0.0f, 0.0f});
		if (material.roughnessTexture)     addTexture("RoughnessTexture", material.roughnessTexture);

		if (material.normalTexture)        addTexture("NormalTexture", material.normalTexture);
		if (material.hasNormalMultiplier)  addScalar("NormalMultiplier", Vec4{material.normalMultiplier, 0.0f, 0.0f, 0.0f});

		if (material.hasEmissiveColor)     addScalar("EmissiveColor", material.emissiveColor.ToVec4());
		if (material.hasEmissiveFactor)    addScalar("EmissiveFactor", Vec4{material.emissiveFactor, 0.0f, 0.0f, 0.0f});
		if (material.emissiveTexture)      addTexture("EmissiveTexture", material.emissiveTexture);

		if (material.occlusionTexture)     addTexture("OcclusionTexture", material.occlusionTexture);
		if (material.hasOcclusionStrength) addScalar("OcclusionStrength", Vec4{material.occlusionStrength, 0.0f, 0.0f, 0.0f});

		materialObject.Commit(scope);

		return materialResource;
	}

	RID ImportMaterial(const MaterialImportData& material, const SubResourceAllocator& alloc, StringView subId)
	{
#if SK_IMPORT_MATERIAL_AS_GRAPH_INSTANCE
		return ImportMaterialGraphInstance(material, alloc, subId);
#else
		return ImportMaterialResource(material, alloc, subId);
#endif
	}
}
