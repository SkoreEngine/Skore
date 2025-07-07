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

#pragma once

#include "Skore/Common.hpp"

#include "Device.hpp"
#include "GraphicsResources.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	namespace MeshTools
	{
		SK_API void CalcNormals(Span<MeshStaticVertex> vertices, Span<u32> indices);
		SK_API void CalcTangents(Span<MeshStaticVertex> vertices, Span<u32> indices);
		SK_API void CalcTangents(Span<MeshSkeletalVertex> vertices, Span<u32> indices);
		SK_API u64 GenerateIndices(const Array<MeshStaticVertex>& allVertices, Array<u32>& newIndices, Array<MeshStaticVertex>& newVertices, bool checkForDuplicates = true);
	}

	class SK_API SinglePassDownsampler
	{
	public:
		struct DownscaleData
		{
			Vec4 mipInfo;
		};

		void Init();
		void Downsample(GPUCommandBuffer* cmd, GPUTexture* inputTexture, GPUTexture* outputTexture);
		void Destroy();
	private:
		GPUPipeline* m_pipeline = nullptr;
		GPUBuffer*   m_buffer   = nullptr;
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
}
