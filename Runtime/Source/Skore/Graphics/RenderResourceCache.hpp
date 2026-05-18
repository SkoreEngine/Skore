#pragma once


#include "Device.hpp"
#include "GraphicsResources.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
	struct TextureAssetFlags
	{
		enum
		{
			None                = 0,
			HasBaseColorTexture = 1 << 1,
			HasNormalTexture    = 1 << 2,
			HasRoughnessTexture = 1 << 3,
			HasMetallicTexture  = 1 << 4,
			HasEmissiveTexture  = 1 << 5,
			HasOcclusionTexture = 1 << 6,
		};
	};

	struct ResourceCacheBase
	{
		u64 usage = 0;
		void DecreaseUsage();
		void IncreaseUsage();
	};

	struct TextureResourceCache : ResourceCacheBase
	{
		RID rid = {};
		GPUTexture* texture = nullptr;
		u32 textureIndex = U32_MAX;

		TextureResourceCache(RID rid) : rid(rid) {}
	};

	struct SK_API FontResourceCache
	{
		RID                          rid;
		FontMetrics                  metrics;
		HashMap<u32, FontGlyph>      glyphs;
		HashMap<Pair<u32, u32>, f64> kerning;
		f32                          maxHeightGlyph{};
		GPUTexture*                  texture = nullptr;
		u16                          fontId{};

		bool GetAdvance(f64& advance, u32 codepoint1, u32 codepoint2);
	};

	struct MaterialResourceCache : ResourceCacheBase
	{
		RID rid;

		MaterialResourceCache(RID rid) : rid(rid) {}

		MaterialResource::MaterialType type = MaterialResource::MaterialType::Opaque;
		u32                            materialIndex = U32_MAX;
		GPUBuffer*                     materialBuffer = nullptr;
		GPUDescriptorSet*              descriptorSet = nullptr;

		// Optional custom shader for the opaque GBuffer/depth pass. Null = use engine default.
		RID shader = {};

		//material data.
		TextureResourceCache* skyMaterialTexture = nullptr;
		GPUTexture*           diffuseIrradianceTexture = nullptr;
		GPUTexture*           specularMapTexture = nullptr;

		//MaterialResource::MaterialType needs to be transparent?
		bool transparent = false;
	};

	struct SkinResourceCache : ResourceCacheBase
	{
		RID rid;

		SkinResourceCache(RID rid) : rid(rid) {}

		Array<Mat4> poses;
	};

	struct MeshResourceCache : ResourceCacheBase
	{
		RID rid;

		MeshResourceCache(RID rid) : rid(rid) {}

		GPUBuffer* vertexBuffer = nullptr;
		GPUBuffer* indexBuffer = nullptr;
		Array<GPUBottomLevelAS*> blasArray;
		u32 geometryIndex = U32_MAX;
		u32 vertexLayoutId = U32_MAX;
		u32 stride = 0;
		bool hasUV1 = false;
		bool hasColor = false;
		Vec2 lightmapSizeHint = Vec2(0.0f, 0.0f);

		Array<MeshPrimitive>          primitives;
		Array<MaterialResourceCache*> materials;

		SkinResourceCache* skin = nullptr;
		AABB aabb;
	};

	struct SK_API RenderResourceCache
	{
		static FontResourceCache*     GetFontCache(RID font);
		static TextureResourceCache*  GetTextureCache(RID texture);
		static MaterialResourceCache* GetMaterialCache(RID material);
		static MeshResourceCache*     GetMeshCache(RID mesh);
		static SkinResourceCache*     GetSkinCache(RID cache);
		static GPUDescriptorSet*      GetGlobalDescriptorSet();
		static GPUBuffer*             GetMaterialDataBuffer();
		static u32                    GetMaterialDataCount();
	};
}
