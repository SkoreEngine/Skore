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
#include "Device.hpp"
#include "RenderStorage.hpp"
#include "RenderTools.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	static constexpr u32 s_numCascades = 4;

	class SK_API SceneRendererViewport
	{
	public:
		SceneRendererViewport() = default;
		SK_NO_COPY_CONSTRUCTOR(SceneRendererViewport)

		~SceneRendererViewport();

		void Init();

		void Resize(Extent extent);

		Extent GetExtent() const;

		void SetCamera(f32 nearClip, f32 farClip, const Mat4& view, const Mat4& projection, Vec3 cameraPosition);
		void Render(RenderStorage* storage, GPUCommandBuffer* commandBuffer);
		void Blit(GPURenderPass* renderPass, GPUCommandBuffer* cmd);

	private:
		Extent m_extent{};

		GPUTexture*       attachmentTexture = nullptr;
		GPUTexture*       depthTexture = nullptr;
		GPUTexture*       colorOutputTexture = nullptr;
		GPURenderPass*    renderPass = nullptr;
		GPUPipeline*      opaqueMaterialPipeline = nullptr;
		GPUPipeline*      skyboxMaterialPipeline = nullptr;
		GPUPipeline*      finalCompositePipeline = nullptr;
		GPUDescriptorSet* descriptorSet = nullptr;
		GPUDescriptorSet* lightDescriptorSet = nullptr;
		GPUDescriptorSet* finalCompositeDescriptorSet = nullptr;
		GPUBuffer*        uniformBuffer = nullptr;
		GPUBuffer*        lightBuffer = nullptr;

		Mat4 m_view = Mat4(1.0);
		Mat4 m_projection = Mat4(1.0);
		Vec3 m_cameraPosition;

		f32 m_nearClip = 0.0;
		f32 m_farClip = 0.0;

		//shadow map data

		GPUTexture*       m_shadowMapDepthTexture = nullptr;
		GPUTextureView*   m_shadowMapTextureViews[s_numCascades]{};
		GPURenderPass*    m_shadowMapRenderPass[s_numCascades]{};
		GPUDescriptorSet* m_shadowMapDescriptorSets[s_numCascades]{};
		GPUBuffer*        m_shadowMapUniformBuffer{};
		GPUPipeline*      m_shadowMapPipeline = nullptr;
		GPUSampler*       m_shadowMapSampler = nullptr;

		u32  m_shadowMapSize = 4096;
		f32  m_cascadeSplitLambda = 0.75f;
		f32  m_cascadeSplits[s_numCascades]{};
		Mat4 m_cascadeViewProjMat[s_numCascades];

		//sky and environment data
		MaterialStorageData* m_skyMaterial = nullptr;
		GPUTexture*    m_diffuseIrradianceTexture = nullptr;
		GPUTexture*    m_specularTexture = nullptr;
		BRDFLUTTexture m_brdflutTexture;

		void PrepareEnvironment(RenderStorage* storage, GPUCommandBuffer* cmd);
		void RenderShadows(RenderStorage* storage, GPUCommandBuffer* cmd);


		void Destroy();
	};
}
