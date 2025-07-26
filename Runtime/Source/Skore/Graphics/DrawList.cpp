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

#include "DrawList.hpp"

#include "Device.hpp"
#include "Graphics.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	DrawList2D::DrawList2D()
	{
		vertices.Reserve(1000);
		indices.Reserve(1000);
	}

	DrawList2D::~DrawList2D()
	{
		if (drawListPipeline) drawListPipeline->Destroy();
		if (vertexBuffer) vertexBuffer->Destroy();
		if (indexBuffer) indexBuffer->Destroy();
	}

	void DrawList2D::AddRectFilled(const Vec2& min, const Vec2& max, const Color& color)
	{
		vertices.EmplaceBack(Vertex{.position = {min.x, min.y}, .color = Color::ToU32(color)});
		vertices.EmplaceBack(Vertex{.position = {max.x, min.y}, .color = Color::ToU32(color)});
		vertices.EmplaceBack(Vertex{.position = {max.x, max.y}, .color = Color::ToU32(color)});
		vertices.EmplaceBack(Vertex{.position = {min.x, max.y}, .color = Color::ToU32(color)});

		indices.EmplaceBack(0);
		indices.EmplaceBack(1);
		indices.EmplaceBack(2);
		indices.EmplaceBack(2);
		indices.EmplaceBack(3);
		indices.EmplaceBack(0);
	}

	void DrawList2D::DrawItems(GPURenderPass* renderPass, GPUCommandBuffer* cmd)
	{
		if (RID rid = Resources::FindByPath("Skore://Shaders/DrawList2D.raster"))
		{
			if (!drawListPipeline)
			{
				drawListPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/DrawList2D.raster"),
					.depthStencilState = {
						.depthTestEnable = false
					},
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = renderPass
				});
			}

			if (!vertexBuffer)
			{
				vertexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = 1000 * sizeof(Vertex),
					.usage = ResourceUsage::VertexBuffer,
					.hostVisible = true,
					.persistentMapped = true
				});
			}

			if (!indexBuffer)
			{
				indexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = 1000 * sizeof(u32),
					.usage = ResourceUsage::IndexBuffer,
					.hostVisible = true,
					.persistentMapped = true
				});
			}

			memcpy(vertexBuffer->GetMappedData(), vertices.Data(), vertices.Size() * sizeof(Vertex));
			memcpy(indexBuffer->GetMappedData(), indices.Data(), indices.Size() * sizeof(u32));

			Extent extent = renderPass->GetExtent();
			Mat4   projection = Math::Ortho(0, extent.width, extent.height, 0, -1, 1);

			cmd->BindPipeline(drawListPipeline);

			cmd->BindVertexBuffer(0, {vertexBuffer}, 0);
			cmd->BindIndexBuffer(indexBuffer, 0, IndexType::Uint32);

			cmd->PushConstants(drawListPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &projection);

			cmd->DrawIndexed(indices.Size(), 1, 0, 0, 0);
		}

		vertices.Clear();
		indices.Clear();
	}
}
