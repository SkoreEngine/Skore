#pragma once

#include "Skore/Common.hpp"

#include "Device.hpp"
#include "GraphicsResources.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	namespace MeshTools
	{
		SK_API void CalcNormals(Span<Vec3> positions, Span<Vec3> normals, Span<u32> indices);
		SK_API void CalcTangents(Span<Vec3> positions, Span<Vec3> normals, Span<Vec2> uvs, Span<Vec4> tangents, Span<u32> indices);
		SK_API void GenerateLightmapUV(f32 lightMapTexelSize, Array<Vec3>& positions, Array<Vec3>& normals, Array<Vec2>& uvs, Array<Vec2>& uv2s,
			Array<Vec3>& colors, Array<Vec4>& tangents, Array<u32>& indices, Array<MeshPrimitive>& primitives, const Vec3& scale, Vec2& lightmapSizeHint);

		SK_API u64 GenerateIndices(const Array<MeshStaticVertex>& allVertices, Array<u32>& newIndices, Array<MeshStaticVertex>& newVertices, bool checkForDuplicates = true);
	}

	namespace RenderTools
	{
		SK_API void TextureResize(GPUCommandBuffer* cmd, GPUTexture* srcTexture,  GPUTexture* dstTexture);
		SK_API void SaveTextureToDisk(GPUTexture* texture, ResourceState currentState, const StringView& directory, const StringView& name);
	}

	//TODO - add half option.
	class SK_API SinglePassDownsampler
	{
	public:
		constexpr static u32 SPD_MAX_MIP_LEVELS = 12;

		SinglePassDownsampler() = default;
		~SinglePassDownsampler();
		SK_NO_COPY_CONSTRUCTOR(SinglePassDownsampler)

		struct SpdConstants
		{
			u32 mips;
			u32 numWorkGroupsPerSlice;
			u32 workGroupOffset[2];
		};

		void Init(GPUTexture* inputTexture);
		void Execute(GPUCommandBuffer* cmd);

	private:
		GPUPipeline*      m_pipeline = nullptr;
		GPUBuffer*        m_buffer = nullptr;
		GPUTexture*       m_texture = nullptr;
		GPUTextureView*   m_textureViews[SPD_MAX_MIP_LEVELS + 1] = {};
		GPUDescriptorSet* m_descriptorSet = nullptr;
	};


	class SK_API BRDFLUTTexture
	{
	public:
		void        Init(Extent extent);
		void        Destroy();
		GPUTexture* GetTexture() const;
		GPUSampler* GetSampler() const;

	private:
		GPUTexture* m_texture = nullptr;
		GPUSampler* m_sampler = nullptr;
	};

	class SK_API EquirectangularToCubeMap
	{
	public:
		void Init();
		void Execute(GPUCommandBuffer* cmd, GPUTexture* equirectangularTexture, GPUTexture* cubeMapTexture);
		void Destroy();
	private:
		GPUPipeline*      m_pipeline = nullptr;
		GPUDescriptorSet* m_descriptorSet = nullptr;
		GPUTextureView*   m_cubeMapTextureView = nullptr;
	};


	class SK_API DiffuseIrradianceGenerator
	{
	public:

		void Init();
		void Execute(GPUCommandBuffer* cmd, GPUTexture* cubemapTexture, GPUTexture* irradianceTexture);
		void Destroy();
	private:
		GPUPipeline*      m_pipeline = nullptr;
		GPUDescriptorSet* m_descriptorSet = nullptr;
		GPUTextureView*   m_irradianceTextureView = nullptr;
	};

	class SK_API SpecularMapGenerator
	{
	public:

		struct SpecularMapFilterSettings
		{
			float roughness;
			float pad;
		};

		SpecularMapGenerator() = default;
		~SpecularMapGenerator();

		void Init(GPUTexture* cubemapTexture, GPUTexture* specularMapTexture);
		void Execute(GPUCommandBuffer* cmd) const;

	private:
		GPUPipeline* m_pipeline = nullptr;
		GPUTexture* m_cubemapTexture;
		GPUTexture* m_specularMapTexture;
		Array<GPUTextureView*> m_views;
	};
}
