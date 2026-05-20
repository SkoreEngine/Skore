#pragma once


#include <future>
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

	struct ResourceCache;
	struct FontResourceCache;
	struct TextureResourceCache;
	struct MaterialResourceCache;
	struct SkinResourceCache;
	struct MeshResourceCache;

	using ResourceCachePtr     = std::shared_ptr<ResourceCache>;
	using FontResourceCachePtr     = std::shared_ptr<FontResourceCache>;
	using TextureResourceCachePtr  = std::shared_ptr<TextureResourceCache>;
	using MaterialResourceCachePtr = std::shared_ptr<MaterialResourceCache>;
	using SkinResourceCachePtr     = std::shared_ptr<SkinResourceCache>;
	using MeshResourceCachePtr     = std::shared_ptr<MeshResourceCache>;

	struct SK_API ResourceCache
	{
		virtual ~ResourceCache() = default;
	};

	struct SK_API TextureResourceCache : ResourceCache
	{
		RID         rid = {};
		GPUTexture* texture = nullptr;
		u32         textureIndex = U32_MAX;

		// Valid while a pixel-data upload is pending on the worker; wait on it to ensure the texture
		// contents are populated before sampling.
		std::shared_future<void> uploadComplete;

		TextureResourceCache(RID rid) : rid(rid) {}
		~TextureResourceCache() override;

		bool IsLoaded() const
		{
			if (!texture) return false;
			if (!uploadComplete.valid()) return true;
			return uploadComplete.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}
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

	struct SK_API MaterialResourceCache : ResourceCache
	{
		RID rid;

		MaterialResourceCache(RID rid) : rid(rid) {}
		~MaterialResourceCache() override;

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

		// Valid while the IBL pre-bake (irradiance + specular) is pending on the worker.
		std::shared_future<void> iblComplete;

		// MaterialResource::MaterialType needs to be transparent?
		bool transparent = false;

		// Set true once the resource-change event has been registered, so the dtor can unregister it.
		bool eventRegistered = false;

		bool IsLoaded() const
		{
			return materialIndex != U32_MAX;
		}
	};

	struct SK_API SkinResourceCache
	{
		RID rid;
		RID mesh;

		SkinResourceCache(RID rid, RID mesh) : rid(rid), mesh(mesh) {}
		~SkinResourceCache() = default;

		Array<Mat4> poses;
	};

	struct SK_API MeshResourceCache : ResourceCache
	{
		RID rid;

		MeshResourceCache(RID rid) : rid(rid) {}
		~MeshResourceCache() override;

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

		AABB aabb;

		// Valid while a vertex/index/BLAS upload is pending on the worker; wait on it to ensure
		// the buffers are populated (and BLAS, if any, is built) before sampling/ray-tracing.
		std::shared_future<void> uploadComplete;

		bool IsLoaded() const
		{
			if (!vertexBuffer || !indexBuffer) return false;
			if (!uploadComplete.valid()) return true;
			return uploadComplete.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}
	};

	struct SK_API RenderResourceCache
	{
		static bool                     WorkerIdle();
		static FontResourceCachePtr     GetFontCache(RID font);
		static TextureResourceCachePtr  GetTextureCache(RID texture, bool async);
		static MaterialResourceCachePtr GetMaterialCache(RID material, bool async);
		static MeshResourceCachePtr     GetMeshCache(RID mesh, bool async);
		static SkinResourceCachePtr     GetSkinCache(RID mesh);
		static GPUDescriptorSet*        GetGlobalDescriptorSet();
		static GPUBuffer*               GetMaterialDataBuffer();
		static u32                      GetMaterialDataCount();

		static void Flush();
	};
}
