#include "Skore/Graphics/DrawList.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	struct DrawListVertex
	{
		Vec2 position;
		Vec2 uv;
		u32  color = 0;
	};

	struct DrawListBatch
	{
		u32 vertexCount = 0;
		u32 indexCount = 0;
		u32 vertexCapacity = 0;
		u32 indexCapacity = 0;

		DrawListVertex* vertices = nullptr;
		u32*            indices = nullptr;

		bool fontRendering = false;

		GPUTexture*       texture = nullptr;
		GPUDescriptorSet* descriptorSet = nullptr;
		GPUBuffer*        vertexBuffer = nullptr;
		GPUBuffer*        indexBuffer = nullptr;

		void CheckBufferSizes()
		{
			u32 vertexBufferSize = vertexCount * sizeof(DrawListVertex);
			u32 indexBufferSize = indexCount * sizeof(u32);

			if (vertexBuffer && vertexBuffer->GetDesc().size < vertexBufferSize)
			{
				vertexBuffer->Destroy();
				vertexBuffer = nullptr;
			}

			if (indexBuffer && indexBuffer->GetDesc().size < indexBufferSize)
			{
				indexBuffer->Destroy();
				indexBuffer = nullptr;
			}

			if (!vertexBuffer)
			{
				vertexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = vertexBufferSize,
					.usage = ResourceUsage::VertexBuffer,
					.hostVisible = true,
					.persistentMapped = true
				});
			}

			if (!indexBuffer)
			{
				indexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = indexBufferSize,
					.usage = ResourceUsage::IndexBuffer,
					.hostVisible = true,
					.persistentMapped = true
				});
			}
		}
	};

	struct ScissorRect
	{
		Vec2 min;
		Vec2 max;
		bool enabled = false;
	};

	struct DrawListContext
	{
		GPUPipeline*          drawListPipeline = nullptr;
		GPUPipeline*          fontDrawListPipeline = nullptr;
		DrawListBatch*        currentBatch;
		Array<DrawListBatch*> currentBatches;

		HashMap<GPUTexture*, Array<DrawListBatch*>> available;
		Array<DrawListBatch*>                       allBatches;

		// Scissor rectangle stack
		Array<ScissorRect> scissorStack;
		ScissorRect        currentScissor;
	};

	DrawListBatch* GetOrCreateContextBatchList(DrawListContext* context, GPUTexture* texture, bool fontRendering)
	{
		if (context->currentBatch && context->currentBatch->texture == texture)
		{
			return context->currentBatch;
		}

		if (auto it = context->available.Find(texture))
		{
			if (!it->second.Empty())
			{
				DrawListBatch* batch = it->second.Back();
				context->currentBatch = batch;
				context->currentBatches.EmplaceBack(batch);
				context->available[texture].PopBack();
				return batch;
			}
		}

		DrawListBatch* batch = Alloc<DrawListBatch>();
		batch->texture = texture;

		batch->vertexCapacity = 100;
		batch->indexCapacity = 150;

		batch->vertices = AllocElements<DrawListVertex>(batch->vertexCapacity);
		batch->indices = AllocElements<u32>(batch->indexCapacity);

		batch->fontRendering = fontRendering;

		batch->descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::SampledImage
				},
				DescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = DescriptorType::Sampler
				}
			},
			.debugName = "DrawListBatch_DescriptorSet"
		});

		batch->descriptorSet->UpdateTexture(0, texture);
		batch->descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());

		context->currentBatch = batch;
		context->currentBatches.EmplaceBack(batch);
		context->allBatches.EmplaceBack(batch);

		return batch;
	}

	DrawListContext* DrawList::CreateContext()
	{
		DrawListContext* context = Alloc<DrawListContext>();
		return context;
	}

	inline void CheckBatchSizes(DrawListBatch* batch, u32 vertexCount, u32 indexCount)
	{
		if (batch->vertexCount + vertexCount > batch->vertexCapacity)
		{
			batch->vertexCapacity = batch->vertexCapacity * 2;
			batch->vertices = ReallocElements<DrawListVertex>(batch->vertices, batch->vertexCapacity);
		}

		if (batch->indexCount + indexCount > batch->indexCapacity)
		{
			batch->indexCapacity = batch->indexCapacity * 2;
			batch->indices = ReallocElements<u32>(batch->indices, batch->indexCapacity);
		}
	}

	// Helper function to check if geometry should be culled by scissor test
	inline bool ShouldCullGeometry(const DrawListContext* context, const Vec2& min, const Vec2& max)
	{
		if (!context->currentScissor.enabled)
			return false;

		// Check if rectangle is completely outside scissor bounds
		return (max.x <= context->currentScissor.min.x || min.x >= context->currentScissor.max.x ||
				max.y <= context->currentScissor.min.y || min.y >= context->currentScissor.max.y);
	}

	// Helper function to clip rectangle to scissor bounds
	inline void ClipRectToScissor(const DrawListContext* context, Vec2& min, Vec2& max, Vec2& uvMin, Vec2& uvMax)
	{
		if (!context->currentScissor.enabled)
			return;

		Vec2 originalMin = min;
		Vec2 originalMax = max;
		Vec2 originalSize = originalMax - originalMin;

		// Clip rectangle to scissor bounds
		min.x = Math::Max(min.x, context->currentScissor.min.x);
		min.y = Math::Max(min.y, context->currentScissor.min.y);
		max.x = Math::Min(max.x, context->currentScissor.max.x);
		max.y = Math::Min(max.y, context->currentScissor.max.y);

		// Adjust UV coordinates proportionally
		if (originalSize.x > 0 && originalSize.y > 0)
		{
			Vec2 uvSize = uvMax - uvMin;
			f32 leftClip = (min.x - originalMin.x) / originalSize.x;
			f32 rightClip = (originalMax.x - max.x) / originalSize.x;
			f32 topClip = (min.y - originalMin.y) / originalSize.y;
			f32 bottomClip = (originalMax.y - max.y) / originalSize.y;

			uvMin.x += uvSize.x * leftClip;
			uvMax.x -= uvSize.x * rightClip;
			uvMin.y += uvSize.y * topClip;
			uvMax.y -= uvSize.y * bottomClip;
		}
	}

	void DrawList::AddRect(DrawListContext* context, const Vec2& min, const Vec2& max, const Color& color, f32 thickness)
	{
		// Draw border as four rectangles (top, right, bottom, left)
		// Top border
		AddRectFilled(context, Vec2(min.x, min.y), Vec2(max.x, min.y + thickness), color);
		// Right border
		AddRectFilled(context, Vec2(max.x - thickness, min.y + thickness), Vec2(max.x, max.y - thickness), color);
		// Bottom border
		AddRectFilled(context, Vec2(min.x, max.y - thickness), Vec2(max.x, max.y), color);
		// Left border
		AddRectFilled(context, Vec2(min.x, min.y + thickness), Vec2(min.x + thickness, max.y - thickness), color);
	}

	void DrawList::PushScissorRect(DrawListContext* context, const Vec2& min, const Vec2& max)
	{
		// Push current scissor state onto the stack
		context->scissorStack.EmplaceBack(context->currentScissor);

		// Calculate new scissor rectangle
		ScissorRect newScissor;
		newScissor.min = min;
		newScissor.max = max;
		newScissor.enabled = true;

		// If there's already a scissor active, intersect with it
		if (context->currentScissor.enabled)
		{
			newScissor.min.x = Math::Max(context->currentScissor.min.x, min.x);
			newScissor.min.y = Math::Max(context->currentScissor.min.y, min.y);
			newScissor.max.x = Math::Min(context->currentScissor.max.x, max.x);
			newScissor.max.y = Math::Min(context->currentScissor.max.y, max.y);

			// Check if intersection is valid
			if (newScissor.min.x >= newScissor.max.x || newScissor.min.y >= newScissor.max.y)
			{
				// Invalid intersection - disable scissor
				newScissor.enabled = false;
			}
		}

		context->currentScissor = newScissor;
	}

	void DrawList::PopScissorRect(DrawListContext* context)
	{
		if (!context->scissorStack.Empty())
		{
			// Restore previous scissor state from stack
			context->currentScissor = context->scissorStack.Back();
			context->scissorStack.PopBack();
		}
		else
		{
			// No more scissor rectangles on stack - disable scissoring
			context->currentScissor.enabled = false;
		}
	}

	void DestroyDrawListBatch(DrawListBatch* batch)
	{
		if (batch->vertices) MemFree(batch->vertices);
		if (batch->indices) MemFree(batch->indices);

		if (batch->vertexBuffer) batch->vertexBuffer->Destroy();
		if (batch->indexBuffer) batch->indexBuffer->Destroy();
		if (batch->descriptorSet) batch->descriptorSet->Destroy();
	}

	void DrawList::DestroyContext(DrawListContext* context)
	{
		if (context->drawListPipeline) context->drawListPipeline->Destroy();
		if (context->fontDrawListPipeline) context->fontDrawListPipeline->Destroy();

		for (DrawListBatch* batch : context->allBatches)
		{
			DestroyDrawListBatch(batch);
		}

		DestroyAndFree(context);
	}

	void DrawList::AddRectFilled(DrawListContext* context, const Vec2& min, const Vec2& max, const Color& color)
	{
		// Early cull test - if completely outside scissor, don't draw
		if (ShouldCullGeometry(context, min, max))
			return;

		Vec2 clippedMin = min;
		Vec2 clippedMax = max;
		Vec2 uvMin = Vec2(0, 0);
		Vec2 uvMax = Vec2(1, 1);

		// Clip rectangle to scissor bounds
		ClipRectToScissor(context, clippedMin, clippedMax, uvMin, uvMax);

		DrawListBatch* batch = GetOrCreateContextBatchList(context, Graphics::GetWhiteTexture(), false);

		CheckBatchSizes(batch, 4, 6);

		u32 firstVertex = batch->vertexCount;

		batch->vertices[batch->vertexCount].position = clippedMin;
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = uvMin;
		batch->vertexCount++;

		batch->vertices[batch->vertexCount].position = {clippedMax.x, clippedMin.y};
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = Vec2(uvMax.x, uvMin.y);
		batch->vertexCount++;

		batch->vertices[batch->vertexCount].position = {clippedMax.x, clippedMax.y};
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = uvMax;
		batch->vertexCount++;

		batch->vertices[batch->vertexCount].position = {clippedMin.x, clippedMax.y};
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = Vec2(uvMin.x, uvMax.y);
		batch->vertexCount++;

		batch->indices[batch->indexCount++] = firstVertex + 0;
		batch->indices[batch->indexCount++] = firstVertex + 1;
		batch->indices[batch->indexCount++] = firstVertex + 2;
		batch->indices[batch->indexCount++] = firstVertex + 2;
		batch->indices[batch->indexCount++] = firstVertex + 3;
		batch->indices[batch->indexCount++] = firstVertex + 0;
	}

	void DrawList::AddText(DrawListContext* context, RID font, const Vec2& position, f32 size, StringView text, const Color& color)
	{
		const float kerning = 0.0f;


		if (FontResourceCachePtr fontCache = RenderResourceCache::GetFontCache(font))
		{
			const TextureDesc& desc = fontCache->texture->GetDesc();

			DrawListBatch* batch = GetOrCreateContextBatchList(context, fontCache->texture, true);
			CheckBatchSizes(batch, text.Size() * 4, text.Size() * 6);

			f32 x = position.x;
			f32 y = position.y;
			f32 startX = x; // Remember the starting x position for line breaks
			f32 lineHeight = 0;

			f32 fsScale = size / (fontCache->metrics.ascenderY - fontCache->metrics.descenderY);
			f32 maxY = 0;

			for (size_t i = 0; i < text.Size(); i++)
			{
				char c = text[i];

				if (c == 0)
				{
					continue;
				}

				// Handle special characters
				if (c == '\n')
				{
					// Move to the next line (based on accumulated line height)
					y -= (lineHeight > 0 ? lineHeight : size);
					x = startX;
					lineHeight = 0;
					continue;
				}
				else if (c == '\r')
				{
					// Carriage return - move to the beginning of the current line
					x = startX;
					continue;
				}

				auto it = fontCache->glyphs.Find(c);
				if (it == fontCache->glyphs.end())
				{
					it = fontCache->glyphs.Find('?');
					if (it == fontCache->glyphs.end())
					{
						return;
					}
				}

				FontGlyph& glyph = it->second;

				Vec2 texCoordMin(glyph.atlasBounds.x, glyph.atlasBounds.w);
				Vec2 texCoordMax(glyph.atlasBounds.z, glyph.atlasBounds.y);

				Vec2 quadMin(glyph.planeBounds.x, fontCache->maxHeightGlyph - glyph.planeBounds.w);
				Vec2 quadMax(glyph.planeBounds.z, fontCache->maxHeightGlyph - glyph.planeBounds.y);

				quadMin *= fsScale;
				quadMax *= fsScale;

				f32 charHeight = quadMax.y + Math::Abs(quadMin.y);
				maxY = Math::Max(maxY, charHeight);
				lineHeight = Math::Max(lineHeight, charHeight);

				quadMin += Vec2(x, y);
				quadMax += Vec2(x, y);

				float texelWidth = 1.0f / desc.extent.width;
				float texelHeight = 1.0f / desc.extent.height;
				texCoordMin *= Vec2(texelWidth, texelHeight);
				texCoordMax *= Vec2(texelWidth, texelHeight);

				// Apply scissor clipping to character quad
				if (!ShouldCullGeometry(context, quadMin, quadMax))
				{
					Vec2 clippedQuadMin = quadMin;
					Vec2 clippedQuadMax = quadMax;
					Vec2 clippedTexCoordMin = texCoordMin;
					Vec2 clippedTexCoordMax = texCoordMax;

					// Clip character quad to scissor bounds
					ClipRectToScissor(context, clippedQuadMin, clippedQuadMax, clippedTexCoordMin, clippedTexCoordMax);

					Vec2 clippedTextCoordMinMax = {clippedTexCoordMin.x, clippedTexCoordMax.y};
					Vec2 clippedTextCoordMaxMin = {clippedTexCoordMax.x, clippedTexCoordMin.y};

					u32 firstVertex = batch->vertexCount;

					batch->vertices[batch->vertexCount].position = clippedQuadMin;
					batch->vertices[batch->vertexCount].color = Color::ToU32(color);
					batch->vertices[batch->vertexCount].uv = clippedTexCoordMin;
					batch->vertexCount++;

					batch->vertices[batch->vertexCount].position = Vec2{clippedQuadMin.x, clippedQuadMax.y};
					batch->vertices[batch->vertexCount].color = Color::ToU32(color);
					batch->vertices[batch->vertexCount].uv = clippedTextCoordMinMax;
					batch->vertexCount++;

					batch->vertices[batch->vertexCount].position = clippedQuadMax;
					batch->vertices[batch->vertexCount].color = Color::ToU32(color);
					batch->vertices[batch->vertexCount].uv = clippedTexCoordMax;
					batch->vertexCount++;

					batch->vertices[batch->vertexCount].position = Vec2{clippedQuadMax.x, clippedQuadMin.y};
					batch->vertices[batch->vertexCount].color = Color::ToU32(color);
					batch->vertices[batch->vertexCount].uv = clippedTextCoordMaxMin;
					batch->vertexCount++;

					batch->indices[batch->indexCount++] = firstVertex + 0;
					batch->indices[batch->indexCount++] = firstVertex + 1;
					batch->indices[batch->indexCount++] = firstVertex + 2;
					batch->indices[batch->indexCount++] = firstVertex + 2;
					batch->indices[batch->indexCount++] = firstVertex + 3;
					batch->indices[batch->indexCount++] = firstVertex + 0;
				}

				if (i < text.Size() - 1)
				{
					f64 advance = glyph.advance;
					fontCache->GetAdvance(advance, c, text[i + 1]);
					x += fsScale * advance + kerning;
				}
			}
		}
	}

	void DrawList::AddImage(DrawListContext* context, GPUTexture* texture, const Vec2& min, const Vec2& max, const Vec2& uvMin, const Vec2& uvMax, const Color& color)
	{
		// Early cull test - if completely outside scissor, don't draw
		if (ShouldCullGeometry(context, min, max))
			return;

		Vec2 clippedMin = min;
		Vec2 clippedMax = max;
		Vec2 clippedUvMin = uvMin;
		Vec2 clippedUvMax = uvMax;

		// Clip rectangle to scissor bounds
		ClipRectToScissor(context, clippedMin, clippedMax, clippedUvMin, clippedUvMax);

		DrawListBatch* batch = GetOrCreateContextBatchList(context, texture, false);
		CheckBatchSizes(batch, 4, 6);

		u32 firstVertex = batch->vertexCount;

		batch->vertices[batch->vertexCount].position = clippedMin;
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = clippedUvMin;
		batch->vertexCount++;

		batch->vertices[batch->vertexCount].position = {clippedMax.x, clippedMin.y};
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = Vec2(clippedUvMax.x, clippedUvMin.y);
		batch->vertexCount++;

		batch->vertices[batch->vertexCount].position = {clippedMax.x, clippedMax.y};
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = clippedUvMax;
		batch->vertexCount++;

		batch->vertices[batch->vertexCount].position = {clippedMin.x, clippedMax.y};
		batch->vertices[batch->vertexCount].color = Color::ToU32(color);
		batch->vertices[batch->vertexCount].uv = Vec2(clippedUvMin.x, clippedUvMax.y);
		batch->vertexCount++;

		batch->indices[batch->indexCount++] = firstVertex + 0;
		batch->indices[batch->indexCount++] = firstVertex + 1;
		batch->indices[batch->indexCount++] = firstVertex + 2;
		batch->indices[batch->indexCount++] = firstVertex + 2;
		batch->indices[batch->indexCount++] = firstVertex + 3;
		batch->indices[batch->indexCount++] = firstVertex + 0;
	}

	Vec2 DrawList::MeasureText(RID font, f32 size, StringView text)
	{
		const float kerning = 0.0f;
		Vec2 dimensions(0.0f, 0.0f);

		if (FontResourceCachePtr fontCache = RenderResourceCache::GetFontCache(font))
		{
			f32 currentLineWidth = 0;
			f32 maxWidth = 0;
			f32 totalHeight = 0;
			f32 lineHeight = 0;
			int lineCount = 1;

			f32 fsScale = size / (fontCache->metrics.ascenderY - fontCache->metrics.descenderY);

			for (size_t i = 0; i < text.Size(); i++)
			{
				char c = text[i];

				if (c == 0)
				{
					continue;
				}

				// Handle special characters
				if (c == '\n')
				{
					// End of line - update maxWidth and prepare for next line
					maxWidth = Math::Max(maxWidth, currentLineWidth);
					currentLineWidth = 0;
					lineCount++;
					continue;
				}
				else if (c == '\r')
				{
					// Carriage return - reset current line width
					currentLineWidth = 0;
					continue;
				}

				auto it = fontCache->glyphs.Find(c);
				if (it == fontCache->glyphs.end())
				{
					it = fontCache->glyphs.Find('?');
					if (it == fontCache->glyphs.end())
					{
						return dimensions;
					}
				}

				FontGlyph& glyph = it->second;

				Vec2 quadMin(glyph.planeBounds.x, fontCache->maxHeightGlyph - glyph.planeBounds.w);
				Vec2 quadMax(glyph.planeBounds.z, fontCache->maxHeightGlyph - glyph.planeBounds.y);

				quadMin *= fsScale;
				quadMax *= fsScale;

				f32 charHeight = quadMax.y + Math::Abs(quadMin.y);
				lineHeight = Math::Max(lineHeight, charHeight);

				if (i < text.Size() - 1)
				{
					f64 advance = glyph.advance;
					fontCache->GetAdvance(advance, c, text[i + 1]);
					currentLineWidth += fsScale * advance + kerning;
				}
				else
				{
					currentLineWidth += quadMax.x - quadMin.x;
				}
			}

			// Check the last line width
			maxWidth = Math::Max(maxWidth, currentLineWidth);

			// Calculate total height for all lines
			totalHeight = lineHeight * lineCount;

			dimensions.x = static_cast<f32>(maxWidth);
			dimensions.y = static_cast<f32>(totalHeight);
		}

		return dimensions;
	}

	void DrawList::Flush(DrawListContext* context, GPURenderPass* renderPass, Extent extent, GPUCommandBuffer* cmd)
	{
		Mat4   projection = Mat4::Ortho(0, extent.width, 0, extent.height, -1, 1);

		if (RID rid = Resources::FindByPath("Skore://Shaders/DrawList2D.raster"))
		{
			if (!context->drawListPipeline)
			{
				context->drawListPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/DrawList2D.raster"),
					.depthStencilState = {
						.depthTestEnable = false
					},
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true
						}
					},
					.renderPass = renderPass
				});
			}

			if (!context->fontDrawListPipeline)
			{
				context->fontDrawListPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/DrawListFont2D.raster"),
					.depthStencilState = {
						.depthTestEnable = false
					},
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true
						}
					},
					.renderPass = renderPass
				});
			}

			for (DrawListBatch* batch : context->currentBatches)
			{
				batch->CheckBufferSizes();

				memcpy(batch->vertexBuffer->GetMappedData(), batch->vertices, batch->vertexCount * sizeof(DrawListVertex));
				memcpy(batch->indexBuffer->GetMappedData(), batch->indices, batch->indexCount * sizeof(u32));

				GPUPipeline* pipeline = batch->fontRendering ? context->fontDrawListPipeline : context->drawListPipeline;

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, batch->descriptorSet, {});

				cmd->BindVertexBuffer(0, {batch->vertexBuffer}, 0);
				cmd->BindIndexBuffer(batch->indexBuffer, 0, IndexType::Uint32);

				if (batch->fontRendering)
				{
					struct FontPushConstants
					{
						Mat4 projection;
						Vec2 fontAtlasSize;
						Vec2 pad;
					};

					const TextureDesc& textureDesc = batch->texture->GetDesc();

					FontPushConstants pushConstants = {};
					pushConstants.projection = projection;
					pushConstants.fontAtlasSize = Vec2(textureDesc.extent.width, textureDesc.extent.height);
					cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(FontPushConstants), &pushConstants);
				}
				else
				{
					cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &projection);
				}

				cmd->DrawIndexed(batch->indexCount, 1, 0, 0, 0);

				batch->vertexCount = 0;
				batch->indexCount = 0;

				auto itAval = context->available.Find(batch->texture);
				if (itAval == context->available.end())
				{
					itAval = context->available.Insert(batch->texture, Array<DrawListBatch*>()).first;
				}
				itAval->second.EmplaceBack(batch);
			}
			context->currentBatch = nullptr;
			context->currentBatches.Clear();
		}
	}
}