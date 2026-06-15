#pragma once


#include <atomic>
#include <future>
#include <memory>

#include "Device.hpp"
#include "GraphicsCommon.hpp"
#include "GraphicsResources.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/OffsetAllocator.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"

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
	struct VertexLayoutCache;

	using ResourceCachePtr     = std::shared_ptr<ResourceCache>;
	using FontResourceCachePtr     = std::shared_ptr<FontResourceCache>;
	using TextureResourceCachePtr  = std::shared_ptr<TextureResourceCache>;
	using MaterialResourceCachePtr = std::shared_ptr<MaterialResourceCache>;
	using SkinResourceCachePtr     = std::shared_ptr<SkinResourceCache>;
	using MeshResourceCachePtr     = std::shared_ptr<MeshResourceCache>;
	using VertexLayoutCachePtr     = std::shared_ptr<VertexLayoutCache>;

	struct SK_API ResourceCache
	{
		virtual ~ResourceCache() = default;
	};

	struct SK_API TextureResourceCache : ResourceCache
	{
		RID         rid = {};
		GPUTexture* texture = nullptr;
		u32         textureIndex = U32_MAX;
		u32         skippedMips = 0;

		// Full-resolution metadata of the underlying texture resource. Used by the streaming path
		// to decide what `skippedMips` value to target for stream-in requests.
		u32 fullExtentWidth  = 0;
		u32 fullExtentHeight = 0;
		u32 fullMipCount     = 0;

		// True while a stream-in/out upload is in flight; gated so we don't queue duplicate tasks.
		std::atomic<bool> streamingPending{false};

		// Aggregated per-frame request across all materials referencing this texture
		// (rebuilt by RenderResourceCache::Flush each frame).
		u32 lastRequestedResolution = 0;

		// Consecutive frames the texture has been over-detailed relative to the request.
		// Eviction triggers once this exceeds the unload-delay threshold.
		u32 streamingDecayFrames = 0;

		// Valid while a pixel-data upload is pending on the worker; wait on it to ensure the texture
		// contents are populated before sampling.
		std::shared_future<void> uploadComplete;

		bool eventRegistered = false;

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
		GPUDescriptorSet*              descriptorSet = nullptr;

		// Keeps PBR textures alive while this material exists.
		Array<TextureResourceCachePtr> textures;

		// Skybox-specific.
		TextureResourceCachePtr skyMaterialTexture;

		GPUTexture*             cubeMapSkyTexture = nullptr;
		GPUTexture*             diffuseIrradianceTexture = nullptr;
		GPUTexture*             specularMapTexture = nullptr;

		// Valid while the IBL pre-bake (irradiance + specular) is pending on the worker.
		std::shared_future<void> iblComplete;

		// MaterialResource::MaterialType needs to be transparent?
		bool transparent = false;
		bool masked = false;

		// Set true once the resource-change event has been registered, so the dtor can unregister it.
		bool eventRegistered = false;

		// texture streaming
		u32 reqTextureResolution = 0;

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

	struct GpuMeshLODInfo
	{
		u32 firstIndex;
		u32 indexCount;
		f32 screenSize;
		f32 pad;
	};

	struct GpuMeshPrimitiveInfo
	{
		u32            lodCount;
		u32            pad[3];
		GpuMeshLODInfo lods[MaxLods];
	};

	static_assert(sizeof(GpuMeshLODInfo) == 16);
	static_assert(sizeof(GpuMeshPrimitiveInfo) == 16 + MaxLods * 16);

	struct SK_API MeshResourceCache : ResourceCache
	{
		RID rid;
		u64 version = 0;

		MeshResourceCache(RID rid) : rid(rid) {}
		~MeshResourceCache() override;

		// Sub-ranges within the shared mesh data buffer. Offsets are in bytes.
		OffsetAllocator::Allocation vertexAlloc{};
		OffsetAllocator::Allocation indexAlloc{};
		u32                      vertexByteOffset = U32_MAX;
		u32                      indexByteOffset = U32_MAX;

		Array<GPUBottomLevelAS*> blasArray;
		u32                      vertexLayoutId = U32_MAX;
		u32                      stride = 0;
		bool                     hasUV1 = false;
		bool                     hasColor = false;
		bool                     hasSkin = false;
		bool                     wantsBlas = false;
		Vec2                     lightmapSizeHint = Vec2(0.0f, 0.0f);

		Array<MeshPrimitive>             primitives;
		Array<u32>                       primitiveInfoSlots;
		Array<MaterialResourceCachePtr>  materials;

		bool                 isProcedural    = false;
		u32                  vertexCount     = 0;
		u32                  vertexAllocSize = 0;
		u32                  indexAllocSize  = 0;
		VertexLayoutCachePtr layoutCache;
		String               debugName;

		AABB aabb;

		// Resolved once the vertex/index data is uploaded. The mesh can be rendered as soon as
		// this is ready — BLAS construction (if any) runs in parallel and reports via
		// blasComplete so ray-tracing pickup can be deferred without holding up scene loading.
		std::shared_future<void> uploadComplete;
		std::shared_future<void> blasComplete;

		bool eventRegistered = false;

		bool IsLoaded() const
		{
			if (vertexByteOffset == U32_MAX || indexByteOffset == U32_MAX) return false;
			if (!uploadComplete.valid()) return true;
			return uploadComplete.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}

		bool IsBlasReady() const
		{
			if (!wantsBlas) return true;
			if (!blasComplete.valid()) return false;
			return blasComplete.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}
	};

	struct SK_API RenderDebug
	{
		static i32& ForcedLod();
	};

	struct VertexLayoutAttribute
	{
		StringView name;
		u32        offset = 0;
		u32        size   = 0;
	};

	struct VertexLayoutDesc
	{
		u32                        stride = 0;
		Span<VertexLayoutAttribute> attributes;
	};

	struct SK_API VertexLayoutCache
	{
		u32  vertexLayoutId = U32_MAX;
		u32  stride         = 0;
		bool hasUV1         = false;
		bool hasColor       = false;
		bool hasSkin        = false;
	};

	struct ProceduralMeshDesc
	{
		VertexLayoutCachePtr           layout;
		Span<u8>                       vertexData;
		u32                            vertexCount = 0;
		Span<u32>                      indices;
		Span<MeshPrimitive>            primitives;
		Span<MaterialResourceCachePtr> materials;
		AABB                           aabb;
		Vec2                           lightmapSizeHint = Vec2(0.0f, 0.0f);
		bool                           wantsBlas = true;
		StringView                     debugName;
	};

	struct ProceduralMeshUpdate
	{
		Span<u8>            vertexData;
		u32                 vertexCount = 0;
		Span<u32>           indices;
		Span<MeshPrimitive> primitives;
		AABB                aabb;
		bool                aabbValid   = false;
		bool                rebuildBlas = false;
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
		static GPUBuffer*               GetMeshDataBuffer();
		static u64                      GetMeshReloadVersion();
		static bool                     GetMeshPrimitiveBlasLod(const MeshResourceCachePtr& mesh, u32 primitiveIndex, u32& firstIndex, u32& indexCount);

		static VertexLayoutCachePtr CreateCustomVoxelLayout(const VertexLayoutDesc& desc);
		static MeshResourceCachePtr CreateProceduralMesh(const ProceduralMeshDesc& desc);
		static void                 UpdateProceduralMesh(const MeshResourceCachePtr& mesh, const ProceduralMeshUpdate& update);

		static void Flush();
		static void Flush(GPUCommandBuffer* cmd);
	};
}
