#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"

namespace Skore
{
	struct SubResourceAllocator;

	enum class MaterialImportAlphaMode
	{
		None,
		Opaque,
		Mask,
		Blend,
	};

	struct MaterialImportData
	{
		StringView name = "Material";

		bool  hasBaseColor = false;
		Color baseColor = Color::WHITE;

		bool  hasEmissiveColor = false;
		Color emissiveColor = Color::BLACK;

		bool hasMetallic = false;
		f32  metallic = 0.0f;

		bool hasRoughness = false;
		f32  roughness = 1.0f;

		bool hasNormalMultiplier = false;
		f32  normalMultiplier = 1.0f;

		bool hasEmissiveFactor = false;
		f32  emissiveFactor = 1.0f;

		bool hasOcclusionStrength = false;
		f32  occlusionStrength = 1.0f;

		bool hasAlphaCutoff = false;
		f32  alphaCutoff = 0.5f;

		bool                    hasAlphaMode = false;
		MaterialImportAlphaMode alphaMode = MaterialImportAlphaMode::Opaque;

		RID baseColorTexture;
		RID normalTexture;
		RID metallicTexture;
		RID roughnessTexture;
		RID emissiveTexture;
		RID occlusionTexture;

		bool           hasMetallicTextureChannel = false;
		TextureChannel metallicTextureChannel = TextureChannel::Red;

		bool           hasRoughnessTextureChannel = false;
		TextureChannel roughnessTextureChannel = TextureChannel::Red;

		bool           hasOcclusionTextureChannel = false;
		TextureChannel occlusionTextureChannel = TextureChannel::Red;
	};

	SK_API RID ImportMaterial(const MaterialImportData& material, const SubResourceAllocator& alloc, StringView subId);
}
