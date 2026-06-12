#include "RenderInterfaceSkore.hpp"

#include <cstring>

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Logger.hpp"

#include <RmlUi/Core/Vertex.h>

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore.RmlUi");
	}

	struct SkoreCompiledGeometry
	{
		GPUBuffer* vertexBuffer = nullptr;
		GPUBuffer* indexBuffer = nullptr;
		u32        indexCount = 0;
	};

	struct SkoreTexture
	{
		GPUTexture*       texture = nullptr;
		GPUDescriptorSet* descriptorSet = nullptr;
		bool              owned = false;
		bool              isMsdf = false;

		TextureResourceCachePtr cache;
	};

	struct MsdfPushConstants
	{
		Mat4 transform;
		Vec2 atlasSize;
		Vec2 pad;
	};

	namespace
	{
		GPUDescriptorSet* CreateTextureDescriptorSet(GPUTexture* texture)
		{
			GPUDescriptorSet* set = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{.binding = 0, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 1, .descriptorType = DescriptorType::Sampler}
				},
				.debugName = "RmlUiTexture_DescriptorSet"
			});
			set->UpdateTexture(0, texture);
			set->UpdateSampler(1, Graphics::GetLinearSampler());
			return set;
		}
	}

	Rml::CompiledGeometryHandle RenderInterfaceSkore::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
	{
		SkoreCompiledGeometry* geom = Alloc<SkoreCompiledGeometry>();
		geom->indexCount = static_cast<u32>(indices.size());

		geom->vertexBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = vertices.size() * sizeof(Rml::Vertex),
			.usage = ResourceUsage::VertexBuffer,
			.hostVisible = true,
			.persistentMapped = true,
		});

		geom->indexBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = indices.size() * sizeof(int),
			.usage = ResourceUsage::IndexBuffer,
			.hostVisible = true,
			.persistentMapped = true,
		});

		memcpy(geom->vertexBuffer->GetMappedData(), vertices.data(), vertices.size() * sizeof(Rml::Vertex));
		memcpy(geom->indexBuffer->GetMappedData(), indices.data(), indices.size() * sizeof(int));

		return reinterpret_cast<Rml::CompiledGeometryHandle>(geom);
	}

	void RenderInterfaceSkore::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
	{
		if (!currentCmd || geometry == 0)
		{
			return;
		}

		SkoreCompiledGeometry* geom = reinterpret_cast<SkoreCompiledGeometry*>(geometry);

		SkoreTexture* skoreTexture = texture != 0 ? reinterpret_cast<SkoreTexture*>(texture) : nullptr;
		bool          useMsdf = skoreTexture && skoreTexture->isMsdf;

		GPUPipeline* pipeline = EnsurePipeline(useMsdf);
		if (!pipeline)
		{
			return;
		}

		currentCmd->BindPipeline(pipeline);

		GPUDescriptorSet* descriptorSet = skoreTexture ? skoreTexture->descriptorSet : GetWhiteDescriptorSet();
		currentCmd->BindDescriptorSet(pipeline, 0, descriptorSet, {});

		Mat4 mvp = projection * transform * Mat4::Translate(Vec3(translation.x, translation.y, 0.0f));
		if (useMsdf)
		{
			const TextureDesc& atlasDesc = skoreTexture->texture->GetDesc();
			MsdfPushConstants  pc;
			pc.transform = mvp;
			pc.atlasSize = Vec2(static_cast<f32>(atlasDesc.extent.width), static_cast<f32>(atlasDesc.extent.height));
			pc.pad = Vec2(0.0f, 0.0f);
			currentCmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(MsdfPushConstants), &pc);
		}
		else
		{
			currentCmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &mvp);
		}

		if (scissorEnabled)
		{
			i32 left = Math::Max(scissorRegion.Left(), 0);
			i32 top = Math::Max(scissorRegion.Top(), 0);
			i32 right = Math::Min(scissorRegion.Right(), static_cast<i32>(viewport.width));
			i32 bottom = Math::Min(scissorRegion.Bottom(), static_cast<i32>(viewport.height));
			currentCmd->SetScissor(Vec2(left, top), Extent{static_cast<u32>(Math::Max(right - left, 0)), static_cast<u32>(Math::Max(bottom - top, 0))});
		}
		else
		{
			currentCmd->SetScissor(Vec2(0, 0), viewport);
		}

		currentCmd->BindVertexBuffer(0, geom->vertexBuffer, 0);
		currentCmd->BindIndexBuffer(geom->indexBuffer, 0, IndexType::Uint32);
		currentCmd->DrawIndexed(geom->indexCount, 1, 0, 0, 0);
	}

	void RenderInterfaceSkore::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
	{
		if (geometry == 0)
		{
			return;
		}

		SkoreCompiledGeometry* geom = reinterpret_cast<SkoreCompiledGeometry*>(geometry);
		if (geom->vertexBuffer) geom->vertexBuffer->Destroy();
		if (geom->indexBuffer) geom->indexBuffer->Destroy();
		DestroyAndFree(geom);
	}

	Rml::TextureHandle RenderInterfaceSkore::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
	{
		RID rid = Resources::FindByPath(StringView(source.c_str(), source.size()));
		if (!rid)
		{
			logger.Warn("RmlUi could not resolve texture '{}' through Resources", source.c_str());
			return 0;
		}

		TextureResourceCachePtr cache = RenderResourceCache::GetTextureCache(rid, false);
		if (!cache || !cache->texture)
		{
			logger.Warn("RmlUi texture '{}' has no GPU texture available", source.c_str());
			return 0;
		}

		const TextureDesc& desc = cache->texture->GetDesc();
		texture_dimensions.x = static_cast<int>(desc.extent.width);
		texture_dimensions.y = static_cast<int>(desc.extent.height);

		SkoreTexture* skoreTexture = Alloc<SkoreTexture>();
		skoreTexture->texture = cache->texture;
		skoreTexture->owned = false;
		skoreTexture->cache = cache;
		skoreTexture->descriptorSet = CreateTextureDescriptorSet(cache->texture);
		return reinterpret_cast<Rml::TextureHandle>(skoreTexture);
	}

	Rml::TextureHandle RenderInterfaceSkore::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
	{
		GPUTexture* texture = Graphics::CreateTexture(TextureDesc{
			.extent = {static_cast<u32>(source_dimensions.x), static_cast<u32>(source_dimensions.y), 1},
			.format = TextureFormat::R8G8B8A8_UNORM,
			.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
			.debugName = "RmlUiTexture",
		});

		Graphics::UploadTextureData(TextureDataInfo{
			.texture = texture,
			.data = reinterpret_cast<const u8*>(source.data()),
			.size = source.size(),
		});

		SkoreTexture* skoreTexture = Alloc<SkoreTexture>();
		skoreTexture->texture = texture;
		skoreTexture->owned = true;
		skoreTexture->descriptorSet = CreateTextureDescriptorSet(texture);
		return reinterpret_cast<Rml::TextureHandle>(skoreTexture);
	}

	void RenderInterfaceSkore::ReleaseTexture(Rml::TextureHandle texture)
	{
		if (texture == 0)
		{
			return;
		}

		SkoreTexture* skoreTexture = reinterpret_cast<SkoreTexture*>(texture);
		if (skoreTexture->descriptorSet) skoreTexture->descriptorSet->Destroy();
		if (skoreTexture->owned && skoreTexture->texture) skoreTexture->texture->Destroy();
		DestroyAndFree(skoreTexture);
	}

	void RenderInterfaceSkore::EnableScissorRegion(bool enable)
	{
		scissorEnabled = enable;
	}

	void RenderInterfaceSkore::SetScissorRegion(Rml::Rectanglei region)
	{
		scissorRegion = region;
	}

	void RenderInterfaceSkore::SetTransform(const Rml::Matrix4f* newTransform)
	{
		if (newTransform)
		{
			memcpy(&transform, newTransform->data(), sizeof(f32) * 16);
		}
		else
		{
			transform = Mat4(1.0);
		}
	}

	void RenderInterfaceSkore::BeginFrame(GPUCommandBuffer* cmd, GPURenderPass* renderPass, Extent viewportSize)
	{
		currentCmd = cmd;
		frameRenderPass = renderPass;
		viewport = viewportSize;
		projection = Mat4::Ortho(0.0f, static_cast<f32>(viewportSize.width), 0.0f, static_cast<f32>(viewportSize.height), -1.0f, 1.0f);
		transform = Mat4(1.0);
		scissorEnabled = false;
	}

	void RenderInterfaceSkore::EndFrame()
	{
		currentCmd = nullptr;
		frameRenderPass = nullptr;
	}

	GPUPipeline* RenderInterfaceSkore::EnsurePipeline(bool msdf)
	{
		GPUPipeline*&  target = msdf ? msdfPipeline : pipeline;
		GPURenderPass*& targetRenderPass = msdf ? msdfPipelineRenderPass : pipelineRenderPass;

		if (target && targetRenderPass == frameRenderPass)
		{
			return target;
		}

		if (target)
		{
			target->Destroy();
			target = nullptr;
		}

		const char* shaderPath = msdf ? "Skore://Shaders/RmlUiFont.raster" : "Skore://Shaders/RmlUi.raster";
		RID         shader = Resources::FindByPath(shaderPath);
		if (!shader)
		{
			logger.Error("{} shader not found", shaderPath);
			return nullptr;
		}

		target = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
			.shader = shader,
			.depthStencilState = {
				.depthTestEnable = false,
				.depthWriteEnable = false,
			},
			.blendStates = {
				BlendStateDesc{
					.blendEnable = true,
					.srcColorBlendFactor = BlendFactor::One,
					.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha,
					.colorBlendOp = BlendOp::Add,
					.srcAlphaBlendFactor = BlendFactor::One,
					.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha,
					.alphaBlendOp = BlendOp::Add,
				}
			},
			.renderPass = frameRenderPass,
			.debugName = msdf ? "RmlUiFontPipeline" : "RmlUiPipeline",
			.vertexInputStride = sizeof(Rml::Vertex),
		});
		targetRenderPass = frameRenderPass;
		return target;
	}

	Rml::TextureHandle RenderInterfaceSkore::CreateMsdfTextureHandle(GPUTexture* atlas)
	{
		SkoreTexture* skoreTexture = Alloc<SkoreTexture>();
		skoreTexture->texture = atlas;
		skoreTexture->owned = false;
		skoreTexture->isMsdf = true;
		skoreTexture->descriptorSet = CreateTextureDescriptorSet(atlas);
		return reinterpret_cast<Rml::TextureHandle>(skoreTexture);
	}

	GPUDescriptorSet* RenderInterfaceSkore::GetWhiteDescriptorSet()
	{
		if (!whiteDescriptorSet)
		{
			whiteDescriptorSet = CreateTextureDescriptorSet(Graphics::GetWhiteTexture());
		}
		return whiteDescriptorSet;
	}

	void RenderInterfaceSkore::Destroy()
	{
		if (pipeline)
		{
			pipeline->Destroy();
			pipeline = nullptr;
		}
		if (msdfPipeline)
		{
			msdfPipeline->Destroy();
			msdfPipeline = nullptr;
		}
		if (whiteDescriptorSet)
		{
			whiteDescriptorSet->Destroy();
			whiteDescriptorSet = nullptr;
		}
	}
}
