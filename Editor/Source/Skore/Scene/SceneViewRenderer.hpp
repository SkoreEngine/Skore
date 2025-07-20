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
#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/Device.hpp"

namespace Skore
{
	class SceneEditor;

	class SK_API SceneViewRenderer
	{
	public:

		SceneViewRenderer() = default;
		~SceneViewRenderer();

		void Resize(Extent extent);
		void Render(SceneEditor* sceneEditor, GPURenderPass* renderPass, GPUDescriptorSet* sceneDescriptorSet, GPUCommandBuffer* commads);
		void Blit(SceneEditor* sceneEditor, GPURenderPass* renderPass, GPUDescriptorSet* sceneDescriptorSet, GPUCommandBuffer* commads);

		bool drawGrid = true;
		bool drawSelectionOutline = true;
		bool drawDebugPhysics = true;
	private:
		Extent currentExtent;

		//selection outline
		GPUTexture* maskTexture = nullptr;
		GPURenderPass* maskRenderPass = nullptr;
		GPUTexture* compositeMaskTexture = nullptr;
		GPURenderPass* compositeMaskRenderPass = nullptr;
		GPUPipeline* maskPipeline = nullptr;
		GPUPipeline* compositeMaskPipeline = nullptr;
		GPUPipeline* debugPhysicsPipeline = nullptr;
		GPUDescriptorSet* maskDescriptorSet = nullptr;
		GPUDescriptorSet* compositeMaskDescriptorSet = nullptr;

		GPUPipeline* unlitPipeline = nullptr;

		//infinite grid.
		GPUPipeline* gridPipeline = nullptr;
	};
}
