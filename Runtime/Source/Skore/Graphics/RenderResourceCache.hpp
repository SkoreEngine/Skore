#pragma once


#include <memory>

#include "Device.hpp"
#include "GraphicsResources.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
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

	struct FontResourceCache;
	struct TextureResourceCache;
	struct MaterialResourceCache;
	struct SkinResourceCache;
	struct MeshResourceCache;

	using FontResourceCachePtr     = std::shared_ptr<FontResourceCache>;
	using TextureResourceCachePtr  = std::shared_ptr<TextureResourceCache>;
	using MaterialResourceCachePtr = std::shared_ptr<MaterialResourceCache>;
	using SkinResourceCachePtr     = std::shared_ptr<SkinResourceCache>;
	using MeshResourceCachePtr     = std::shared_ptr<MeshResourceCache>;

	struct SK_API TextureResourceCache
	{
		RID         rid = {};
		GPUTexture* texture = nullptr;
		u32         textureIndex = U32_MAX;

		TextureResourceCache(RID rid) : rid(rid) {}
		~TextureResourceCache();
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

		FontResourceCache(RID rid) : rid(rid) {}
		~FontResourceCache();

		bool GetAdvance(f64& advance, u32 codepoint1, u32 codepoint2);
	};

	struct SK_API MaterialResourceCache
	{
		RID rid;

		MaterialResourceCache(RID rid) : rid(rid) {}
		~MaterialResourceCache();

		MaterialResource::MaterialType type = MaterialResource::MaterialType::Opaque;
		u32                            materialIndex = U32_MAX;
		GPUBuffer*                     materialBuffer = nullptr;
		GPUDescriptorSet*              descriptorSet = nullptr;

		// Optional custom shader for the opaque GBuffer/depth pass. Null = use engine default.
		RID shader = {};

		// Keeps PBR textures alive while this material exists.
		Array<TextureResourceCachePtr> textures;

		// Skybox-specific.
		TextureResourceCachePtr skyMaterialTexture;
		GPUTexture*             diffuseIrradianceTexture = nullptr;
		GPUTexture*             specularMapTexture = nullptr;

		// MaterialResource::MaterialType needs to be transparent?
		bool transparent = false;

		// Set true once the resource-change event has been registered, so the dtor can unregister it.
		bool eventRegistered = false;
	};

	struct SK_API SkinResourceCache
	{
		RID rid;

		SkinResourceCache(RID rid) : rid(rid) {}
		~SkinResourceCache() = default;

		Array<Mat4> poses;
	};

	struct SK_API MeshResourceCache
	{
		RID rid;

		MeshResourceCache(RID rid) : rid(rid) {}
		~MeshResourceCache();

		GPUBuffer*               vertexBuffer = nullptr;
		GPUBuffer*               indexBuffer = nullptr;
		Array<GPUBottomLevelAS*> blasArray;
		u32                      geometryIndex = U32_MAX;
		u32                      vertexLayoutId = U32_MAX;
		u32                      stride = 0;
		bool                     hasUV1 = false;
		bool                     hasColor = false;
		Vec2                     lightmapSizeHint = Vec2(0.0f, 0.0f);

		Array<MeshPrimitive>             primitives;
		Array<MaterialResourceCachePtr>  materials;

		SkinResourceCachePtr skin;
		AABB                 aabb;
	};

	struct SK_API RenderResourceCache
	{
		static FontResourceCachePtr     GetFontCache(RID font);
		static TextureResourceCachePtr  GetTextureCache(RID texture);
		static MaterialResourceCachePtr GetMaterialCache(RID material);
		static MeshResourceCachePtr     GetMeshCache(RID mesh);
		static SkinResourceCachePtr     GetSkinCache(RID cache);
		static GPUDescriptorSet*        GetGlobalDescriptorSet();
		static GPUBuffer*               GetMaterialDataBuffer();
		static u32                      GetMaterialDataCount();
	};
}
