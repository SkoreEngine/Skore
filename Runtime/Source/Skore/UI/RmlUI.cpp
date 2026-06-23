#include "RmlUI.hpp"
#include "Skore/Scene/Components/UIDocument.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include "Skore/App.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/IO/Input.hpp"
#include "Skore/IO/InputEvents.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Scene/Scene.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Texture.h>
#include <RmlUi/Core/Vertex.h>

namespace Skore
{
	class RenderInterfaceSkore;

	namespace
	{
		Logger& logger = Logger::GetLogger("Skore.RmlUi");
		RenderInterfaceSkore* renderInterface = nullptr;

		Array<RID>* dependencyCapture = nullptr;

		void CaptureDependency(RID rid)
		{
			if (dependencyCapture && rid && dependencyCapture->IndexOf(rid) == nPos)
			{
				dependencyCapture->EmplaceBack(rid);
			}
		}
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

	class RenderInterfaceSkore : public Rml::RenderInterface
	{
	public:
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
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

		void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override
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

		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
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

		Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override
		{
			RID rid = Resources::FindByPath(StringView(source.c_str(), source.size()));
			if (!rid)
			{
				logger.Warn("RmlUi could not resolve texture '{}' through Resources", source.c_str());
				return 0;
			}

			CaptureDependency(rid);

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

		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override
		{
			GPUTexture* texture = Graphics::CreateTexture(TextureDesc{
				.extent = {static_cast<u32>(source_dimensions.x), static_cast<u32>(source_dimensions.y), 1},
				.format = Format::RGBA8_UNORM,
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

		void ReleaseTexture(Rml::TextureHandle texture) override
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

		void EnableScissorRegion(bool enable) override
		{
			scissorEnabled = enable;
		}

		void SetScissorRegion(Rml::Rectanglei region) override
		{
			scissorRegion = region;
		}

		void SetTransform(const Rml::Matrix4f* newTransform) override
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

		void BeginFrame(GPUCommandBuffer* cmd, GPURenderPass* renderPass, Extent viewportSize)
		{
			currentCmd = cmd;
			frameRenderPass = renderPass;
			viewport = viewportSize;
			projection = Mat4::Ortho(0.0f, static_cast<f32>(viewportSize.width), 0.0f, static_cast<f32>(viewportSize.height), -1.0f, 1.0f);
			transform = Mat4(1.0);
			scissorEnabled = false;
		}

		void EndFrame()
		{
			currentCmd = nullptr;
			frameRenderPass = nullptr;
		}

		Rml::TextureHandle CreateMsdfTextureHandle(GPUTexture* atlas)
		{
			SkoreTexture* skoreTexture = Alloc<SkoreTexture>();
			skoreTexture->texture = atlas;
			skoreTexture->owned = false;
			skoreTexture->isMsdf = true;
			skoreTexture->descriptorSet = CreateTextureDescriptorSet(atlas);
			return reinterpret_cast<Rml::TextureHandle>(skoreTexture);
		}

		void Destroy()
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

	private:
		GPUPipeline* EnsurePipeline(bool msdf)
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

		GPUDescriptorSet* GetWhiteDescriptorSet()
		{
			if (!whiteDescriptorSet)
			{
				whiteDescriptorSet = CreateTextureDescriptorSet(Graphics::GetWhiteTexture());
			}
			return whiteDescriptorSet;
		}

		GPUCommandBuffer* currentCmd = nullptr;
		GPURenderPass*    frameRenderPass = nullptr;
		Extent            viewport = {};
		Mat4              projection = Mat4(1.0);

		GPUPipeline*   pipeline = nullptr;
		GPURenderPass* pipelineRenderPass = nullptr;

		GPUPipeline*   msdfPipeline = nullptr;
		GPURenderPass* msdfPipelineRenderPass = nullptr;

		GPUDescriptorSet* whiteDescriptorSet = nullptr;

		Mat4 transform = Mat4(1.0);

		bool            scissorEnabled = false;
		Rml::Rectanglei scissorRegion = {};
	};

	class SystemInterfaceSkore : public Rml::SystemInterface
	{
	public:
		SystemInterfaceSkore() : startTime(Platform::GetTime()) {}

		double GetElapsedTime() override
		{
			return static_cast<double>(Platform::GetTime() - startTime) / 1.0e9;
		}

		bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
		{
			switch (type)
			{
				case Rml::Log::LT_ERROR:
				case Rml::Log::LT_ASSERT:
					logger.Error("{}", message.c_str());
					break;
				case Rml::Log::LT_WARNING:
					logger.Warn("{}", message.c_str());
					break;
				default:
					logger.Info("{}", message.c_str());
					break;
			}
			return true;
		}

		void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override
		{
			Input::SetTextInputActive(true);
		}

		void DeactivateKeyboard() override
		{
			Input::SetTextInputActive(false);
		}

	private:
		u64 startTime = 0;
	};

	namespace
	{
		struct SkoreFontFace
		{
			FontResourceCachePtr       cache;
			RID                        rid;
			int                        size = 0;
			f64                        scale = 1.0;
			Rml::FontMetrics           metrics = {};
			Rml::CallbackTextureSource textureSource;
		};

		HashMap<String, RID>  registeredFonts;
		Array<SkoreFontFace*> faces;

		RID ResolveFamily(StringView family)
		{
			String upperFamily = ToUpper(family);
			for (RID font : Resources::GetResourcesByType(Resources::FindType<FontResource>()))
			{
				ResourceObject fontObject = Resources::Read(font);
				if (ToUpper(fontObject.GetString(FontResource::Name)) == upperFamily)
				{
					return font;
				}
			}
			return {};
		}

		void DecodeCodepoints(Rml::StringView string, Array<u32>& out)
		{
			for (auto it = Rml::StringIteratorU8(string); it; ++it)
			{
				out.EmplaceBack(static_cast<u32>(*it));
			}
		}
	}

	class FontEngineSkore : public Rml::FontEngineInterface
	{
	public:
		FontEngineSkore() = default;

		~FontEngineSkore() override
		{
			ReleaseFontResources();
		}

		void RegisterFont(StringView family, RID fontResource)
		{
			registeredFonts.Insert(String(family), fontResource);
		}

		void Shutdown() override
		{
			ReleaseFontResources();
		}

		Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size) override
		{
			RID rid = ResolveFamily(StringView(family.c_str(), family.size()));
			if (!rid)
			{
				return 0;
			}

			for (SkoreFontFace* existing : faces)
			{
				if (existing->rid == rid && existing->size == size)
				{
					return reinterpret_cast<Rml::FontFaceHandle>(existing);
				}
			}

			FontResourceCachePtr cache = RenderResourceCache::GetFontCache(rid);
			if (!cache || !cache->texture)
			{
				return 0;
			}

			SkoreFontFace* face = Alloc<SkoreFontFace>();
			face->cache = cache;
			face->rid = rid;
			face->size = size;

			const f64 emSize = cache->metrics.emSize != 0.0 ? cache->metrics.emSize : 1.0;
			face->scale = static_cast<f64>(size) / emSize;

			face->metrics.size = size;
			face->metrics.ascent = static_cast<f32>(cache->metrics.ascenderY * face->scale);
			face->metrics.descent = static_cast<f32>(-cache->metrics.descenderY * face->scale);
			face->metrics.line_spacing = static_cast<f32>(cache->metrics.lineHeight * face->scale);
			face->metrics.underline_position = static_cast<f32>(-cache->metrics.underlineY * face->scale);
			face->metrics.underline_thickness = static_cast<f32>(cache->metrics.underlineThickness * face->scale);

			auto xIt = cache->glyphs.Find('x');
			if (xIt != cache->glyphs.end())
			{
				face->metrics.x_height = static_cast<f32>(xIt->second.planeBounds.w * face->scale);
			}
			else
			{
				face->metrics.x_height = face->metrics.ascent * 0.5f;
			}
			face->metrics.has_ellipsis = cache->glyphs.Find(0x2026) != cache->glyphs.end();

			GPUTexture* atlas = cache->texture;
			face->textureSource = Rml::CallbackTextureSource([atlas](const Rml::CallbackTextureInterface& textureInterface) -> bool
			{
				if (!renderInterface)
				{
					return false;
				}
				const TextureDesc& desc = atlas->GetDesc();
				Rml::TextureHandle  handle = renderInterface->CreateMsdfTextureHandle(atlas);
				textureInterface.SetTextureHandle(handle, Rml::Vector2i(static_cast<int>(desc.extent.width), static_cast<int>(desc.extent.height)));
				return true;
			});

			faces.EmplaceBack(face);
			return reinterpret_cast<Rml::FontFaceHandle>(face);
		}

		const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override
		{
			return reinterpret_cast<SkoreFontFace*>(handle)->metrics;
		}

		int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& text_shaping_context, Rml::Character prior_character) override
		{
			SkoreFontFace* face = reinterpret_cast<SkoreFontFace*>(handle);
			if (!face)
			{
				return 0;
			}

			Array<u32> cps;
			DecodeCodepoints(string, cps);

			f64 width = 0.0;
			for (usize i = 0; i < cps.Size(); ++i)
			{
				auto it = face->cache->glyphs.Find(cps[i]);
				if (it == face->cache->glyphs.end())
				{
					it = face->cache->glyphs.Find('?');
					if (it == face->cache->glyphs.end())
					{
						continue;
					}
				}

				f64 advance = it->second.advance;
				u32 next = (i + 1 < cps.Size()) ? cps[i + 1] : 0;
				face->cache->GetAdvance(advance, cps[i], next);
				width += advance * face->scale;
			}
			return static_cast<int>(std::round(width));
		}

		int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle face_handle, Rml::FontEffectsHandle font_effects_handle, Rml::StringView string,
		                   Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& text_shaping_context,
		                   Rml::TexturedMeshList& mesh_list) override
		{
			SkoreFontFace* face = reinterpret_cast<SkoreFontFace*>(face_handle);
			if (!face)
			{
				return 0;
			}

			const TextureDesc& atlasDesc = face->cache->texture->GetDesc();
			const f32          atlasW = static_cast<f32>(atlasDesc.extent.width);
			const f32          atlasH = static_cast<f32>(atlasDesc.extent.height);

			Rml::TexturedMesh textured;
			textured.texture = face->textureSource.GetTexture(render_manager);
			Rml::Mesh& mesh = textured.mesh;

			Array<u32> cps;
			DecodeCodepoints(string, cps);

			f64 penX = position.x;
			for (usize i = 0; i < cps.Size(); ++i)
			{
				auto it = face->cache->glyphs.Find(cps[i]);
				if (it == face->cache->glyphs.end())
				{
					it = face->cache->glyphs.Find('?');
					if (it == face->cache->glyphs.end())
					{
						continue;
					}
				}
				const FontGlyph& glyph = it->second;

				const f32 quadW = static_cast<f32>(glyph.planeBounds.z - glyph.planeBounds.x);
				const f32 quadH = static_cast<f32>(glyph.planeBounds.w - glyph.planeBounds.y);
				if (quadW > 0.0f && quadH > 0.0f)
				{
					const f32 x0 = static_cast<f32>(penX + glyph.planeBounds.x * face->scale);
					const f32 x1 = static_cast<f32>(penX + glyph.planeBounds.z * face->scale);
					const f32 y0 = static_cast<f32>(position.y - glyph.planeBounds.w * face->scale);
					const f32 y1 = static_cast<f32>(position.y - glyph.planeBounds.y * face->scale);

					const f32 u0 = static_cast<f32>(glyph.atlasBounds.x) / atlasW;
					const f32 u1 = static_cast<f32>(glyph.atlasBounds.z) / atlasW;
					const f32 v0 = static_cast<f32>(glyph.atlasBounds.w) / atlasH;
					const f32 v1 = static_cast<f32>(glyph.atlasBounds.y) / atlasH;

					const int base = static_cast<int>(mesh.vertices.size());
					mesh.vertices.push_back(Rml::Vertex{Rml::Vector2f(x0, y0), colour, Rml::Vector2f(u0, v0)});
					mesh.vertices.push_back(Rml::Vertex{Rml::Vector2f(x0, y1), colour, Rml::Vector2f(u0, v1)});
					mesh.vertices.push_back(Rml::Vertex{Rml::Vector2f(x1, y1), colour, Rml::Vector2f(u1, v1)});
					mesh.vertices.push_back(Rml::Vertex{Rml::Vector2f(x1, y0), colour, Rml::Vector2f(u1, v0)});
					mesh.indices.push_back(base + 0);
					mesh.indices.push_back(base + 1);
					mesh.indices.push_back(base + 2);
					mesh.indices.push_back(base + 2);
					mesh.indices.push_back(base + 3);
					mesh.indices.push_back(base + 0);
				}

				f64 advance = glyph.advance;
				u32 next = (i + 1 < cps.Size()) ? cps[i + 1] : 0;
				face->cache->GetAdvance(advance, cps[i], next);
				penX += advance * face->scale;
			}

			if (mesh)
			{
				mesh_list.push_back(std::move(textured));
			}
			return static_cast<int>(penX - position.x);
		}

		int GetVersion(Rml::FontFaceHandle handle) override
		{
			return 1;
		}

		void ReleaseFontResources() override
		{
			for (SkoreFontFace* face : faces)
			{
				DestroyAndFree(face);
			}
			faces.Clear();
		}
	};

	namespace
	{
		struct SkoreFile
		{
			String content;
			usize  pos = 0;
		};
	}

	class FileInterfaceSkore : public Rml::FileInterface
	{
	public:
		Rml::FileHandle Open(const Rml::String& path) override
		{
			RID rid = Resources::FindByPath(StringView(path.c_str(), path.size()));
			if (!rid)
			{
				logger.Warn("RmlUi could not resolve file '{}' through Resources", path.c_str());
				return 0;
			}

			CaptureDependency(rid);

			ResourceObject object = Resources::Read(rid);
			if (!object)
			{
				return 0;
			}

			SkoreFile* file = Alloc<SkoreFile>();
			file->content = object.GetString(object.GetIndex("Content"));
			return reinterpret_cast<Rml::FileHandle>(file);
		}

		void Close(Rml::FileHandle file) override
		{
			DestroyAndFree(reinterpret_cast<SkoreFile*>(file));
		}

		size_t Read(void* buffer, size_t size, Rml::FileHandle file) override
		{
			SkoreFile* skoreFile = reinterpret_cast<SkoreFile*>(file);
			usize      remaining = skoreFile->content.Size() - skoreFile->pos;
			usize      toRead = Math::Min(static_cast<usize>(size), remaining);
			memcpy(buffer, skoreFile->content.CStr() + skoreFile->pos, toRead);
			skoreFile->pos += toRead;
			return toRead;
		}

		bool Seek(Rml::FileHandle file, long offset, int origin) override
		{
			SkoreFile* skoreFile = reinterpret_cast<SkoreFile*>(file);
			usize      size = skoreFile->content.Size();
			i64        base = 0;

			switch (origin)
			{
				case SEEK_SET: base = 0; break;
				case SEEK_CUR: base = static_cast<i64>(skoreFile->pos); break;
				case SEEK_END: base = static_cast<i64>(size); break;
				default: return false;
			}

			i64 newPos = base + offset;
			if (newPos < 0 || newPos > static_cast<i64>(size))
			{
				return false;
			}

			skoreFile->pos = static_cast<usize>(newPos);
			return true;
		}

		size_t Tell(Rml::FileHandle file) override
		{
			return reinterpret_cast<SkoreFile*>(file)->pos;
		}
	};

	namespace
	{
		SystemInterfaceSkore* systemInterface = nullptr;
		FontEngineSkore*      fontEngine = nullptr;
		FileInterfaceSkore*   fileInterface = nullptr;

		struct ContextEntry
		{
			Rml::Context* context = nullptr;
			Vec2          offset = {0, 0};
			f32           scale = 1.0f;
			bool          visible = true;
			bool          resourceSync = false;
		};

		Array<ContextEntry*> contexts;
		std::mutex           contextsMutex;

		ContextEntry* GetContextEntry(UIContext* context)
		{
			return reinterpret_cast<ContextEntry*>(context);
		}

		Rml::Context* GetRmlContext(UIContext* context)
		{
			if (ContextEntry* entry = GetContextEntry(context))
			{
				return entry->context;
			}
			return nullptr;
		}

		bool IsContextVisible(UIContext* context)
		{
			if (ContextEntry* entry = GetContextEntry(context))
			{
				return entry->visible;
			}
			return false;
		}

		UIContext* FindContextEntry(Rml::Context* context)
		{
			if (!context)
			{
				return nullptr;
			}

			std::scoped_lock lock(contextsMutex);
			for (ContextEntry* entry : contexts)
			{
				if (entry->context == context)
				{
					return reinterpret_cast<UIContext*>(entry);
				}
			}

			return nullptr;
		}

		struct DocumentEntry
		{
			Rml::ElementDocument* document = nullptr;
			UIContext*            context = nullptr;
			RID                   resource = {};
			Array<RID>            dependencies = {};
			bool                  resourceSync = false;
			bool                  needsReload = false;
		};

		Array<DocumentEntry*> documents;

		DocumentEntry* GetDocumentEntry(UIElementDocument* document)
		{
			return reinterpret_cast<DocumentEntry*>(document);
		}

		Rml::ElementDocument* GetRmlDocument(UIElementDocument* document)
		{
			if (DocumentEntry* entry = GetDocumentEntry(document))
			{
				return entry->document;
			}
			return nullptr;
		}

		UIElementDocument* FindDocumentEntry(Rml::ElementDocument* rmlDocument)
		{
			if (!rmlDocument)
			{
				return nullptr;
			}

			for (DocumentEntry* entry : documents)
			{
				if (entry->document == rmlDocument)
				{
					return reinterpret_cast<UIElementDocument*>(entry);
				}
			}

			return nullptr;
		}

		void OnDocumentResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			static_cast<DocumentEntry*>(userData)->needsReload = true;
		}

		void RegisterDocumentEvents(DocumentEntry* entry)
		{
			if (!entry->resourceSync)
			{
				return;
			}

			if (entry->resource)
			{
				Resources::GetStorage(entry->resource)->RegisterEvent(ResourceEventType::Changed, OnDocumentResourceChange, entry);
			}

			for (RID dependency : entry->dependencies)
			{
				if (dependency != entry->resource)
				{
					Resources::GetStorage(dependency)->RegisterEvent(ResourceEventType::Changed, OnDocumentResourceChange, entry);
				}
			}
		}

		void UnregisterDocumentEvents(DocumentEntry* entry)
		{
			if (!entry->resourceSync)
			{
				return;
			}

			if (entry->resource)
			{
				Resources::GetStorage(entry->resource)->UnregisterEvent(ResourceEventType::Changed, OnDocumentResourceChange, entry);
			}

			for (RID dependency : entry->dependencies)
			{
				if (dependency != entry->resource)
				{
					Resources::GetStorage(dependency)->UnregisterEvent(ResourceEventType::Changed, OnDocumentResourceChange, entry);
				}
			}
		}

		void LoadDocumentIntoEntry(DocumentEntry* entry)
		{
			Rml::Context* rmlContext = GetRmlContext(entry->context);
			if (!rmlContext || !entry->resource)
			{
				return;
			}

			ResourceObject object = Resources::Read(entry->resource);
			if (!object)
			{
				return;
			}

			String content = object.GetString(UIDocumentResource::Content);
			if (content.Empty())
			{
				return;
			}

			StringView sourceUrl = Resources::GetPath(entry->resource);

			entry->dependencies.Clear();
			Array<RID>* previousCapture = dependencyCapture;
			dependencyCapture = &entry->dependencies;

			entry->document = rmlContext->LoadDocumentFromMemory(
				Rml::String(content.CStr(), content.Size()),
				Rml::String(sourceUrl.Data(), sourceUrl.Size()));

			dependencyCapture = previousCapture;
		}

		void ReloadDocumentEntry(DocumentEntry* entry)
		{
			Rml::Context* rmlContext = GetRmlContext(entry->context);
			if (!rmlContext)
			{
				return;
			}

			UnregisterDocumentEvents(entry);

			bool wasVisible = entry->document != nullptr && entry->document->IsVisible();

			if (entry->document)
			{
				rmlContext->UnloadDocument(entry->document);
				entry->document = nullptr;
			}

			LoadDocumentIntoEntry(entry);
			RegisterDocumentEvents(entry);

			if (wasVisible && entry->document)
			{
				entry->document->Show();
			}
		}

		void ProcessPendingReloads(UIContext* context)
		{
			for (DocumentEntry* entry : documents)
			{
				if (entry->context == context && entry->needsReload)
				{
					entry->needsReload = false;
					ReloadDocumentEntry(entry);
				}
			}
		}

		Rml::Input::KeyIdentifier ToRmlKey(Key key)
		{
			switch (key)
			{
				case Key::Space: return Rml::Input::KI_SPACE;
				case Key::Apostrophe: return Rml::Input::KI_OEM_7;
				case Key::Comma: return Rml::Input::KI_OEM_COMMA;
				case Key::Minus: return Rml::Input::KI_OEM_MINUS;
				case Key::Period: return Rml::Input::KI_OEM_PERIOD;
				case Key::Slash: return Rml::Input::KI_OEM_2;
				case Key::Num0: return Rml::Input::KI_0;
				case Key::Num1: return Rml::Input::KI_1;
				case Key::Num2: return Rml::Input::KI_2;
				case Key::Num3: return Rml::Input::KI_3;
				case Key::Num4: return Rml::Input::KI_4;
				case Key::Num5: return Rml::Input::KI_5;
				case Key::Num6: return Rml::Input::KI_6;
				case Key::Num7: return Rml::Input::KI_7;
				case Key::Num8: return Rml::Input::KI_8;
				case Key::Num9: return Rml::Input::KI_9;
				case Key::Semicolon: return Rml::Input::KI_OEM_1;
				case Key::Equal: return Rml::Input::KI_OEM_PLUS;
				case Key::A: return Rml::Input::KI_A;
				case Key::B: return Rml::Input::KI_B;
				case Key::C: return Rml::Input::KI_C;
				case Key::D: return Rml::Input::KI_D;
				case Key::E: return Rml::Input::KI_E;
				case Key::F: return Rml::Input::KI_F;
				case Key::G: return Rml::Input::KI_G;
				case Key::H: return Rml::Input::KI_H;
				case Key::I: return Rml::Input::KI_I;
				case Key::J: return Rml::Input::KI_J;
				case Key::K: return Rml::Input::KI_K;
				case Key::L: return Rml::Input::KI_L;
				case Key::M: return Rml::Input::KI_M;
				case Key::N: return Rml::Input::KI_N;
				case Key::O: return Rml::Input::KI_O;
				case Key::P: return Rml::Input::KI_P;
				case Key::Q: return Rml::Input::KI_Q;
				case Key::R: return Rml::Input::KI_R;
				case Key::S: return Rml::Input::KI_S;
				case Key::T: return Rml::Input::KI_T;
				case Key::U: return Rml::Input::KI_U;
				case Key::V: return Rml::Input::KI_V;
				case Key::W: return Rml::Input::KI_W;
				case Key::X: return Rml::Input::KI_X;
				case Key::Y: return Rml::Input::KI_Y;
				case Key::Z: return Rml::Input::KI_Z;
				case Key::LeftBracket: return Rml::Input::KI_OEM_4;
				case Key::Backslash: return Rml::Input::KI_OEM_5;
				case Key::RightBracket: return Rml::Input::KI_OEM_6;
				case Key::GraveAccent: return Rml::Input::KI_OEM_3;
				case Key::Escape: return Rml::Input::KI_ESCAPE;
				case Key::Enter: return Rml::Input::KI_RETURN;
				case Key::Tab: return Rml::Input::KI_TAB;
				case Key::Backspace: return Rml::Input::KI_BACK;
				case Key::Insert: return Rml::Input::KI_INSERT;
				case Key::Delete: return Rml::Input::KI_DELETE;
				case Key::Right: return Rml::Input::KI_RIGHT;
				case Key::Left: return Rml::Input::KI_LEFT;
				case Key::Down: return Rml::Input::KI_DOWN;
				case Key::Up: return Rml::Input::KI_UP;
				case Key::PageUp: return Rml::Input::KI_PRIOR;
				case Key::PageDown: return Rml::Input::KI_NEXT;
				case Key::Home: return Rml::Input::KI_HOME;
				case Key::End: return Rml::Input::KI_END;
				case Key::CapsLock: return Rml::Input::KI_CAPITAL;
				case Key::ScrollLock: return Rml::Input::KI_SCROLL;
				case Key::NumLock: return Rml::Input::KI_NUMLOCK;
				case Key::PrintScreen: return Rml::Input::KI_SNAPSHOT;
				case Key::Pause: return Rml::Input::KI_PAUSE;
				case Key::F1: return Rml::Input::KI_F1;
				case Key::F2: return Rml::Input::KI_F2;
				case Key::F3: return Rml::Input::KI_F3;
				case Key::F4: return Rml::Input::KI_F4;
				case Key::F5: return Rml::Input::KI_F5;
				case Key::F6: return Rml::Input::KI_F6;
				case Key::F7: return Rml::Input::KI_F7;
				case Key::F8: return Rml::Input::KI_F8;
				case Key::F9: return Rml::Input::KI_F9;
				case Key::F10: return Rml::Input::KI_F10;
				case Key::F11: return Rml::Input::KI_F11;
				case Key::F12: return Rml::Input::KI_F12;
				case Key::F13: return Rml::Input::KI_F13;
				case Key::F14: return Rml::Input::KI_F14;
				case Key::F15: return Rml::Input::KI_F15;
				case Key::F16: return Rml::Input::KI_F16;
				case Key::F17: return Rml::Input::KI_F17;
				case Key::F18: return Rml::Input::KI_F18;
				case Key::F19: return Rml::Input::KI_F19;
				case Key::F20: return Rml::Input::KI_F20;
				case Key::F21: return Rml::Input::KI_F21;
				case Key::F22: return Rml::Input::KI_F22;
				case Key::F23: return Rml::Input::KI_F23;
				case Key::F24: return Rml::Input::KI_F24;
				case Key::Keypad0: return Rml::Input::KI_NUMPAD0;
				case Key::Keypad1: return Rml::Input::KI_NUMPAD1;
				case Key::Keypad2: return Rml::Input::KI_NUMPAD2;
				case Key::Keypad3: return Rml::Input::KI_NUMPAD3;
				case Key::Keypad4: return Rml::Input::KI_NUMPAD4;
				case Key::Keypad5: return Rml::Input::KI_NUMPAD5;
				case Key::Keypad6: return Rml::Input::KI_NUMPAD6;
				case Key::Keypad7: return Rml::Input::KI_NUMPAD7;
				case Key::Keypad8: return Rml::Input::KI_NUMPAD8;
				case Key::Keypad9: return Rml::Input::KI_NUMPAD9;
				case Key::KeypadDecimal: return Rml::Input::KI_DECIMAL;
				case Key::KeypadDivide: return Rml::Input::KI_DIVIDE;
				case Key::KeypadMultiply: return Rml::Input::KI_MULTIPLY;
				case Key::KeypadSubtract: return Rml::Input::KI_SUBTRACT;
				case Key::KeypadAdd: return Rml::Input::KI_ADD;
				case Key::KeypadEnter: return Rml::Input::KI_NUMPADENTER;
				case Key::KeypadEqual: return Rml::Input::KI_OEM_NEC_EQUAL;
				case Key::LeftShift: return Rml::Input::KI_LSHIFT;
				case Key::LeftCtrl: return Rml::Input::KI_LCONTROL;
				case Key::LeftAlt: return Rml::Input::KI_LMENU;
				case Key::LeftSuper: return Rml::Input::KI_LWIN;
				case Key::RightShift: return Rml::Input::KI_RSHIFT;
				case Key::RightCtrl: return Rml::Input::KI_RCONTROL;
				case Key::RightAlt: return Rml::Input::KI_RMENU;
				case Key::RightSuper: return Rml::Input::KI_RWIN;
				case Key::Menu: return Rml::Input::KI_APPS;
				default: return Rml::Input::KI_UNKNOWN;
			}
		}

		int ToRmlMouseButton(MouseButton button)
		{
			switch (button)
			{
				case MouseButton::Left: return 0;
				case MouseButton::Right: return 1;
				case MouseButton::Middle: return 2;
				case MouseButton::X1: return 3;
				case MouseButton::X2: return 4;
				default: return 0;
			}
		}

		int GetRmlModifiers()
		{
			int modifiers = 0;
			if (Input::IsKeyDown(Key::LeftCtrl) || Input::IsKeyDown(Key::RightCtrl)) modifiers |= Rml::Input::KM_CTRL;
			if (Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift)) modifiers |= Rml::Input::KM_SHIFT;
			if (Input::IsKeyDown(Key::LeftAlt) || Input::IsKeyDown(Key::RightAlt)) modifiers |= Rml::Input::KM_ALT;
			if (Input::IsKeyDown(Key::LeftSuper) || Input::IsKeyDown(Key::RightSuper)) modifiers |= Rml::Input::KM_META;
			return modifiers;
		}

		void OnKeyDownEvent(Key key, bool repeat)
		{
			Rml::Input::KeyIdentifier identifier = ToRmlKey(key);
			int                       modifiers = GetRmlModifiers();
			std::scoped_lock          lock(contextsMutex);
			for (const ContextEntry* entry : contexts)
			{
				if (!entry->visible) continue;
				entry->context->ProcessKeyDown(identifier, modifiers);
			}
		}

		void OnKeyUpEvent(Key key)
		{
			Rml::Input::KeyIdentifier identifier = ToRmlKey(key);
			int                       modifiers = GetRmlModifiers();
			std::scoped_lock          lock(contextsMutex);
			for (const ContextEntry* entry : contexts)
			{
				if (!entry->visible) continue;
				entry->context->ProcessKeyUp(identifier, modifiers);
			}
		}

		void OnTextInputEvent(StringView text)
		{
			Rml::String      string(text.Data(), text.Size());
			std::scoped_lock lock(contextsMutex);
			for (const ContextEntry* entry : contexts)
			{
				if (!entry->visible) continue;
				entry->context->ProcessTextInput(string);
			}
		}

		void OnMouseMoveEvent(Vec2 position)
		{
			int              modifiers = GetRmlModifiers();
			std::scoped_lock lock(contextsMutex);
			for (const ContextEntry* entry : contexts)
			{
				if (!entry->visible) continue;
				Vec2 local = (position - entry->offset) * entry->scale;
				entry->context->ProcessMouseMove(static_cast<int>(local.x), static_cast<int>(local.y), modifiers);
			}
		}

		void OnMouseButtonEvent(MouseButton button, bool pressed)
		{
			int              index = ToRmlMouseButton(button);
			int              modifiers = GetRmlModifiers();
			std::scoped_lock lock(contextsMutex);
			for (const ContextEntry* entry : contexts)
			{
				if (!entry->visible) continue;
				if (pressed)
				{
					entry->context->ProcessMouseButtonDown(index, modifiers);
				}
				else
				{
					entry->context->ProcessMouseButtonUp(index, modifiers);
				}
			}
		}

		void OnMouseScrollEvent(Vec2 delta)
		{
			int              modifiers = GetRmlModifiers();
			std::scoped_lock lock(contextsMutex);
			for (const ContextEntry* entry : contexts)
			{
				if (!entry->visible) continue;
				entry->context->ProcessMouseWheel(Rml::Vector2f(-delta.x, -delta.y), modifiers);
			}
		}
	}


	UIContext* UIContext::Create(StringView name, Extent dimensions, bool enableResourceSync)
	{
		Rml::Context* context = Rml::CreateContext(Rml::String(name.Data(), name.Size()),
		                                           Rml::Vector2i(static_cast<int>(dimensions.width), static_cast<int>(dimensions.height)));
		if (!context)
		{
			return nullptr;
		}

		ContextEntry* entry = Alloc<ContextEntry>();
		entry->context = context;
		entry->resourceSync = enableResourceSync;
		{
			std::scoped_lock lock(contextsMutex);
			contexts.EmplaceBack(entry);
		}
		return reinterpret_cast<UIContext*>(entry);
	}

	void UIContext::Destroy()
	{
		ContextEntry* entry = GetContextEntry(this);
		if (!entry)
		{
			return;
		}

		{
			std::scoped_lock lock(contextsMutex);
			for (auto it = contexts.begin(); it != contexts.end(); ++it)
			{
				if (*it == entry)
				{
					contexts.Erase(it);
					break;
				}
			}
		}

		for (auto it = documents.begin(); it != documents.end();)
		{
			DocumentEntry* documentEntry = *it;
			if (documentEntry->context == this)
			{
				UnregisterDocumentEvents(documentEntry);
				DestroyAndFree(documentEntry);
				it = documents.Erase(it);
			}
			else
			{
				++it;
			}
		}

		if (Rml::Context* rmlContext = entry->context)
		{
			Rml::RemoveContext(rmlContext->GetName());
		}

		DestroyAndFree(entry);
	}

	void UIContext::SetDimensions(Extent dimensions)
	{
		if (Rml::Context* rmlContext = GetRmlContext(this))
		{
			rmlContext->SetDimensions(Rml::Vector2i(static_cast<int>(dimensions.width), static_cast<int>(dimensions.height)));
		}
	}

	void UIContext::SetDensityIndependentPixelRatio(f32 ratio)
	{
		if (Rml::Context* rmlContext = GetRmlContext(this))
		{
			rmlContext->SetDensityIndependentPixelRatio(ratio);
		}
	}

	void UIContext::SetInputTransform(Vec2 offset, f32 scale)
	{
		if (ContextEntry* entry = GetContextEntry(this))
		{
			entry->offset = offset;
			entry->scale = scale;
		}
	}

	void UIContext::Update()
	{
		ProcessPendingReloads(this);

		if (Rml::Context* rmlContext = GetRmlContext(this))
		{
			rmlContext->Update();
		}
	}

	void UIContext::Render()
	{
		if (Rml::Context* rmlContext = GetRmlContext(this))
		{
			rmlContext->Render();
		}
	}

	void UIContext::SetVisible(bool visible)
	{
		if (ContextEntry* entry = GetContextEntry(this))
		{
			entry->visible = visible;
		}
	}

	bool UIContext::IsVisible()
	{
		if (ContextEntry* entry = GetContextEntry(this))
		{
			return entry->visible;
		}
		return false;
	}

	UIElementDocument* UIContext::LoadDocumentFromMemory(StringView content)
	{
		Rml::Context* rmlContext = GetRmlContext(this);
		if (!rmlContext)
		{
			return nullptr;
		}

		Rml::ElementDocument* element = rmlContext->LoadDocumentFromMemory(Rml::String(content.Data(), content.Size()));
		if (!element)
		{
			return nullptr;
		}

		DocumentEntry* entry = Alloc<DocumentEntry>();
		entry->document = element;
		entry->context = this;
		documents.EmplaceBack(entry);
		return reinterpret_cast<UIElementDocument*>(entry);
	}

	UIElementDocument* UIContext::LoadDocumentFromResource(RID document)
	{
		ContextEntry* contextEntry = GetContextEntry(this);
		if (!contextEntry || !contextEntry->context || !document)
		{
			return nullptr;
		}

		DocumentEntry* entry = Alloc<DocumentEntry>();
		entry->context = this;
		entry->resource = document;
		entry->resourceSync = contextEntry->resourceSync;

		LoadDocumentIntoEntry(entry);
		if (!entry->document)
		{
			DestroyAndFree(entry);
			return nullptr;
		}

		documents.EmplaceBack(entry);
		RegisterDocumentEvents(entry);
		return reinterpret_cast<UIElementDocument*>(entry);
	}

	void UIContext::UnloadDocument(UIElementDocument* document)
	{
		DocumentEntry* entry = GetDocumentEntry(document);
		if (!entry)
		{
			return;
		}

		UnregisterDocumentEvents(entry);

		if (Rml::Context* rmlContext = GetRmlContext(this); rmlContext && entry->document)
		{
			rmlContext->UnloadDocument(entry->document);
		}

		for (auto it = documents.begin(); it != documents.end(); ++it)
		{
			if (*it == entry)
			{
				documents.Erase(it);
				break;
			}
		}

		DestroyAndFree(entry);
	}

	namespace
	{
		Rml::ModalFlag ToRmlModalFlag(UIModalFlag flag)
		{
			switch (flag)
			{
				case UIModalFlag::Modal: return Rml::ModalFlag::Modal;
				case UIModalFlag::Keep: return Rml::ModalFlag::Keep;
				default: return Rml::ModalFlag::None;
			}
		}

		Rml::FocusFlag ToRmlFocusFlag(UIFocusFlag flag)
		{
			switch (flag)
			{
				case UIFocusFlag::None: return Rml::FocusFlag::None;
				case UIFocusFlag::Document: return Rml::FocusFlag::Document;
				case UIFocusFlag::Keep: return Rml::FocusFlag::Keep;
				default: return Rml::FocusFlag::Auto;
			}
		}

		Rml::ScrollFlag ToRmlScrollFlag(UIScrollFlag flag)
		{
			switch (flag)
			{
				case UIScrollFlag::None: return Rml::ScrollFlag::None;
				default: return Rml::ScrollFlag::Auto;
			}
		}
	}

	void UIElementDocument::Show(UIModalFlag modalFlag, UIFocusFlag focusFlag, UIScrollFlag scrollFlag)
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->Show(ToRmlModalFlag(modalFlag), ToRmlFocusFlag(focusFlag), ToRmlScrollFlag(scrollFlag));
		}
	}

	void UIElementDocument::Hide()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->Hide();
		}
	}

	void UIElementDocument::Close()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->Close();
		}
	}

	void UIElementDocument::PullToFront()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->PullToFront();
		}
	}

	void UIElementDocument::PushToBack()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->PushToBack();
		}
	}

	void UIElementDocument::SetTitle(StringView title)
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->SetTitle(Rml::String(title.Data(), title.Size()));
		}
	}

	String UIElementDocument::GetTitle()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			const Rml::String& title = element->GetTitle();
			return String(title.c_str(), title.size());
		}
		return {};
	}

	String UIElementDocument::GetSourceURL()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			const Rml::String& url = element->GetSourceURL();
			return String(url.c_str(), url.size());
		}
		return {};
	}

	bool UIElementDocument::IsModal()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			return element->IsModal();
		}
		return false;
	}

	void UIElementDocument::ReloadStyleSheet()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->ReloadStyleSheet();
		}
	}

	void UIElementDocument::Update()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			element->UpdateDocument();
		}
	}

	UIContext* UIElementDocument::GetContext()
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			return FindContextEntry(element->GetContext());
		}
		return nullptr;
	}

	UIElement* UIElementDocument::FindNextTabElement(UIElement* currentElement, bool forward, bool wrapAround)
	{
		if (Rml::ElementDocument* element = GetRmlDocument(this))
		{
			return reinterpret_cast<UIElement*>(element->FindNextTabElement(reinterpret_cast<Rml::Element*>(currentElement), forward, wrapAround));
		}
		return nullptr;
	}

	namespace
	{
		Rml::String ToRmlString(StringView view)
		{
			return Rml::String(view.Data(), view.Size());
		}

		String FromRmlString(const Rml::String& string)
		{
			return String(string.c_str(), string.size());
		}

		Array<UIElement*> ToElementArray(const Rml::ElementList& list)
		{
			Array<UIElement*> result;
			result.Reserve(list.size());
			for (Rml::Element* element : list)
			{
				result.EmplaceBack(reinterpret_cast<UIElement*>(element));
			}
			return result;
		}
	}

	UIElement* UIElementDocument::CreateElement(StringView name)
	{
		if (Rml::ElementDocument* doc = GetRmlDocument(this))
		{
			return reinterpret_cast<UIElement*>(doc->CreateElement(ToRmlString(name)).release());
		}
		return nullptr;
	}

	UIElement* UIElementDocument::CreateTextNode(StringView text)
	{
		if (Rml::ElementDocument* doc = GetRmlDocument(this))
		{
			return reinterpret_cast<UIElement*>(doc->CreateTextNode(ToRmlString(text)).release());
		}
		return nullptr;
	}

	UIElement* UIElement::Clone()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->Clone().release());
		}
		return nullptr;
	}

	void UIElement::Destroy()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			Rml::ElementPtr owned(el);
		}
	}

	UIElement* UIElement::AppendChild(UIElement* child)
	{
		Rml::Element* parentElement = reinterpret_cast<Rml::Element*>(this);
		Rml::Element* childElement = reinterpret_cast<Rml::Element*>(child);
		if (parentElement && childElement)
		{
			return reinterpret_cast<UIElement*>(parentElement->AppendChild(Rml::ElementPtr(childElement)));
		}
		return nullptr;
	}

	UIElement* UIElement::InsertBefore(UIElement* child, UIElement* adjacentElement)
	{
		Rml::Element* parentElement = reinterpret_cast<Rml::Element*>(this);
		Rml::Element* childElement = reinterpret_cast<Rml::Element*>(child);
		if (parentElement && childElement)
		{
			return reinterpret_cast<UIElement*>(parentElement->InsertBefore(Rml::ElementPtr(childElement), reinterpret_cast<Rml::Element*>(adjacentElement)));
		}
		return nullptr;
	}

	UIElement* UIElement::RemoveChild(UIElement* child)
	{
		Rml::Element* parentElement = reinterpret_cast<Rml::Element*>(this);
		Rml::Element* childElement = reinterpret_cast<Rml::Element*>(child);
		if (parentElement && childElement)
		{
			return reinterpret_cast<UIElement*>(parentElement->RemoveChild(childElement).release());
		}
		return nullptr;
	}

	bool UIElement::HasChildNodes()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->HasChildNodes();
		}
		return false;
	}

	void UIElement::SetClass(StringView className, bool activate)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetClass(ToRmlString(className), activate);
		}
	}

	bool UIElement::IsClassSet(StringView className)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->IsClassSet(ToRmlString(className));
		}
		return false;
	}

	void UIElement::SetClassNames(StringView classNames)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetClassNames(ToRmlString(classNames));
		}
	}

	String UIElement::GetClassNames()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FromRmlString(el->GetClassNames());
		}
		return {};
	}

	void UIElement::SetPseudoClass(StringView pseudoClass, bool activate)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetPseudoClass(ToRmlString(pseudoClass), activate);
		}
	}

	bool UIElement::IsPseudoClassSet(StringView pseudoClass)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->IsPseudoClassSet(ToRmlString(pseudoClass));
		}
		return false;
	}

	bool UIElement::SetProperty(StringView name, StringView value)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->SetProperty(ToRmlString(name), ToRmlString(value));
		}
		return false;
	}

	void UIElement::RemoveProperty(StringView name)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->RemoveProperty(ToRmlString(name));
		}
	}

	void UIElement::SetAttribute(StringView name, StringView value)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetAttribute(ToRmlString(name), ToRmlString(value));
		}
	}

	String UIElement::GetAttribute(StringView name, StringView defaultValue)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FromRmlString(el->GetAttribute<Rml::String>(ToRmlString(name), ToRmlString(defaultValue)));
		}
		return FromRmlString(ToRmlString(defaultValue));
	}

	bool UIElement::HasAttribute(StringView name)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->HasAttribute(ToRmlString(name));
		}
		return false;
	}

	void UIElement::RemoveAttribute(StringView name)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->RemoveAttribute(ToRmlString(name));
		}
	}

	String UIElement::GetTagName()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FromRmlString(el->GetTagName());
		}
		return {};
	}

	String UIElement::GetId()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FromRmlString(el->GetId());
		}
		return {};
	}

	void UIElement::SetId(StringView id)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetId(ToRmlString(id));
		}
	}

	String UIElement::GetInnerRML()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FromRmlString(el->GetInnerRML());
		}
		return {};
	}

	void UIElement::SetInnerRML(StringView rml)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetInnerRML(ToRmlString(rml));
		}
	}

	bool UIElement::IsVisible(bool includeAncestors)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->IsVisible(includeAncestors);
		}
		return false;
	}

	f32 UIElement::GetAbsoluteLeft()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetAbsoluteLeft();
		}
		return 0.0f;
	}

	f32 UIElement::GetAbsoluteTop()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetAbsoluteTop();
		}
		return 0.0f;
	}

	f32 UIElement::GetClientWidth()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetClientWidth();
		}
		return 0.0f;
	}

	f32 UIElement::GetClientHeight()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetClientHeight();
		}
		return 0.0f;
	}

	f32 UIElement::GetOffsetWidth()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetOffsetWidth();
		}
		return 0.0f;
	}

	f32 UIElement::GetOffsetHeight()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetOffsetHeight();
		}
		return 0.0f;
	}

	f32 UIElement::GetScrollLeft()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetScrollLeft();
		}
		return 0.0f;
	}

	void UIElement::SetScrollLeft(f32 scrollLeft)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetScrollLeft(scrollLeft);
		}
	}

	f32 UIElement::GetScrollTop()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetScrollTop();
		}
		return 0.0f;
	}

	void UIElement::SetScrollTop(f32 scrollTop)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->SetScrollTop(scrollTop);
		}
	}

	f32 UIElement::GetScrollWidth()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetScrollWidth();
		}
		return 0.0f;
	}

	f32 UIElement::GetScrollHeight()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetScrollHeight();
		}
		return 0.0f;
	}

	bool UIElement::Focus(bool focusVisible)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->Focus(focusVisible);
		}
		return false;
	}

	void UIElement::Blur()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->Blur();
		}
	}

	void UIElement::Click()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->Click();
		}
	}

	void UIElement::ScrollIntoView(bool alignWithTop)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			el->ScrollIntoView(alignWithTop);
		}
	}

	UIElement* UIElement::GetParentNode()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetParentNode());
		}
		return nullptr;
	}

	UIElement* UIElement::GetNextSibling()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetNextSibling());
		}
		return nullptr;
	}

	UIElement* UIElement::GetPreviousSibling()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetPreviousSibling());
		}
		return nullptr;
	}

	UIElement* UIElement::GetFirstChild()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetFirstChild());
		}
		return nullptr;
	}

	UIElement* UIElement::GetLastChild()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetLastChild());
		}
		return nullptr;
	}

	UIElement* UIElement::GetChild(i32 index)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetChild(index));
		}
		return nullptr;
	}

	i32 UIElement::GetNumChildren()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->GetNumChildren();
		}
		return 0;
	}

	UIElementDocument* UIElement::GetOwnerDocument()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FindDocumentEntry(el->GetOwnerDocument());
		}
		return nullptr;
	}

	UIContext* UIElement::GetContext()
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return FindContextEntry(el->GetContext());
		}
		return nullptr;
	}

	UIElement* UIElement::GetElementById(StringView id)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->GetElementById(ToRmlString(id)));
		}
		return nullptr;
	}

	UIElement* UIElement::QuerySelector(StringView selector)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->QuerySelector(ToRmlString(selector)));
		}
		return nullptr;
	}

	Array<UIElement*> UIElement::QuerySelectorAll(StringView selector)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			Rml::ElementList list;
			el->QuerySelectorAll(list, ToRmlString(selector));
			return ToElementArray(list);
		}
		return {};
	}

	Array<UIElement*> UIElement::GetElementsByTagName(StringView tag)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			Rml::ElementList list;
			el->GetElementsByTagName(list, ToRmlString(tag));
			return ToElementArray(list);
		}
		return {};
	}

	Array<UIElement*> UIElement::GetElementsByClassName(StringView className)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			Rml::ElementList list;
			el->GetElementsByClassName(list, ToRmlString(className));
			return ToElementArray(list);
		}
		return {};
	}

	UIElement* UIElement::Closest(StringView selectors)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return reinterpret_cast<UIElement*>(el->Closest(ToRmlString(selectors)));
		}
		return nullptr;
	}

	bool UIElement::Matches(StringView selector)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->Matches(ToRmlString(selector));
		}
		return false;
	}

	bool UIElement::Contains(UIElement* other)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->Contains(reinterpret_cast<Rml::Element*>(other));
		}
		return false;
	}

	namespace
	{
		class EventListenerSkore : public Rml::EventListener
		{
		public:
			EventListenerSkore(FnUIEventCallback callback, VoidPtr userData) : callback(callback), userData(userData) {}
			EventListenerSkore(FnUIEvent func) : func(std::move(func)) {}

			void ProcessEvent(Rml::Event& event) override
			{
				if (func)
				{
					func(reinterpret_cast<UIEvent*>(&event));
				}
				else if (callback)
				{
					callback(reinterpret_cast<UIEvent*>(&event), userData);
				}
			}

			void OnDetach(Rml::Element*) override
			{
				DestroyAndFree(this);
			}

		private:
			FnUIEventCallback callback = nullptr;
			VoidPtr           userData = nullptr;
			FnUIEvent         func;
		};
	}

	UIEventListener* UIElement::AddEventListener(StringView event, FnUIEventCallback callback, VoidPtr userData, bool inCapturePhase)
	{
		Rml::Element* el = reinterpret_cast<Rml::Element*>(this);
		if (!el || !callback)
		{
			return nullptr;
		}
		EventListenerSkore* listener = Alloc<EventListenerSkore>(callback, userData);
		el->AddEventListener(ToRmlString(event), listener, inCapturePhase);
		return reinterpret_cast<UIEventListener*>(listener);
	}

	UIEventListener* UIElement::AddEventListener(StringView event, FnUIEvent callback, bool inCapturePhase)
	{
		Rml::Element* el = reinterpret_cast<Rml::Element*>(this);
		if (!el || !callback)
		{
			return nullptr;
		}
		EventListenerSkore* listener = Alloc<EventListenerSkore>(std::move(callback));
		el->AddEventListener(ToRmlString(event), listener, inCapturePhase);
		return reinterpret_cast<UIEventListener*>(listener);
	}

	void UIElement::RemoveEventListener(StringView event, UIEventListener* listener, bool inCapturePhase)
	{
		Rml::Element* el = reinterpret_cast<Rml::Element*>(this);
		if (el && listener)
		{
			el->RemoveEventListener(ToRmlString(event), reinterpret_cast<Rml::EventListener*>(listener), inCapturePhase);
		}
	}

	bool UIElement::DispatchEvent(StringView type)
	{
		if (Rml::Element* el = reinterpret_cast<Rml::Element*>(this))
		{
			return el->DispatchEvent(ToRmlString(type), Rml::Dictionary());
		}
		return false;
	}

	String UIEvent::GetType()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return FromRmlString(ev->GetType());
		}
		return {};
	}

	UIElement* UIEvent::GetTargetElement()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return reinterpret_cast<UIElement*>(ev->GetTargetElement());
		}
		return nullptr;
	}

	UIElement* UIEvent::GetCurrentElement()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return reinterpret_cast<UIElement*>(ev->GetCurrentElement());
		}
		return nullptr;
	}

	UIEventPhase UIEvent::GetPhase()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return static_cast<UIEventPhase>(ev->GetPhase());
		}
		return UIEventPhase::None;
	}

	bool UIEvent::IsInterruptible()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return ev->IsInterruptible();
		}
		return false;
	}

	bool UIEvent::IsPropagating()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return ev->IsPropagating();
		}
		return false;
	}

	void UIEvent::StopPropagation()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			ev->StopPropagation();
		}
	}

	void UIEvent::StopImmediatePropagation()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			ev->StopImmediatePropagation();
		}
	}

	Vec2 UIEvent::GetUnprojectedMouseScreenPos()
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			Rml::Vector2f pos = ev->GetUnprojectedMouseScreenPos();
			return Vec2(pos.x, pos.y);
		}
		return Vec2(0.0f, 0.0f);
	}

	String UIEvent::GetParameterString(StringView key, StringView defaultValue)
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return FromRmlString(ev->GetParameter<Rml::String>(ToRmlString(key), ToRmlString(defaultValue)));
		}
		return FromRmlString(ToRmlString(defaultValue));
	}

	f32 UIEvent::GetParameterFloat(StringView key, f32 defaultValue)
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return ev->GetParameter<float>(ToRmlString(key), defaultValue);
		}
		return defaultValue;
	}

	i32 UIEvent::GetParameterInt(StringView key, i32 defaultValue)
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return ev->GetParameter<int>(ToRmlString(key), defaultValue);
		}
		return defaultValue;
	}

	bool UIEvent::GetParameterBool(StringView key, bool defaultValue)
	{
		if (Rml::Event* ev = reinterpret_cast<Rml::Event*>(this))
		{
			return ev->GetParameter<bool>(ToRmlString(key), defaultValue);
		}
		return defaultValue;
	}

	namespace
	{
		struct UIDataModelConstructorEntry
		{
			Rml::DataModelConstructor constructor;
		};

		Rml::DataModel* ExtractDataModelPtr(Rml::DataModelHandle handle)
		{
			Rml::DataModel* ptr = nullptr;
			static_assert(sizeof(Rml::DataModelHandle) == sizeof(ptr), "DataModelHandle layout mismatch");
			memcpy(&ptr, &handle, sizeof(ptr));
			return ptr;
		}
	}

	UIDataModelConstructor* UIContext::CreateDataModel(StringView name)
	{
		Rml::Context* rmlContext = GetRmlContext(this);
		if (!rmlContext) return nullptr;
		Rml::DataModelConstructor constructor = rmlContext->CreateDataModel(ToRmlString(name));
		if (!constructor) return nullptr;
		UIDataModelConstructorEntry* entry = Alloc<UIDataModelConstructorEntry>();
		entry->constructor = std::move(constructor);
		return reinterpret_cast<UIDataModelConstructor*>(entry);
	}

	UIDataModelConstructor* UIContext::GetDataModel(StringView name)
	{
		Rml::Context* rmlContext = GetRmlContext(this);
		if (!rmlContext) return nullptr;
		Rml::DataModelConstructor constructor = rmlContext->GetDataModel(ToRmlString(name));
		if (!constructor) return nullptr;
		UIDataModelConstructorEntry* entry = Alloc<UIDataModelConstructorEntry>();
		entry->constructor = std::move(constructor);
		return reinterpret_cast<UIDataModelConstructor*>(entry);
	}

	bool UIContext::RemoveDataModel(StringView name)
	{
		if (Rml::Context* rmlContext = GetRmlContext(this))
		{
			return rmlContext->RemoveDataModel(ToRmlString(name));
		}
		return false;
	}

	void UIDataModelConstructor::Destroy()
	{
		if (auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this))
		{
			DestroyAndFree(entry);
		}
	}

	UIDataModel* UIDataModelConstructor::GetModelHandle()
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry) return nullptr;
		return reinterpret_cast<UIDataModel*>(ExtractDataModelPtr(entry->constructor.GetModelHandle()));
	}

	bool UIDataModelConstructor::BindFunc(StringView name,
	                                      FnUIDataGetCallback getCallback, VoidPtr getCallbackData,
	                                      FnUIDataSetCallback setCallback, VoidPtr setCallbackData)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !getCallback) return false;

		Rml::DataGetFunc rmlGet = [getCallback, getCallbackData](Rml::Variant& v) {
			getCallback(reinterpret_cast<UIDataVariant*>(&v), getCallbackData);
		};

		Rml::DataSetFunc rmlSet = {};
		if (setCallback)
		{
			rmlSet = [setCallback, setCallbackData](const Rml::Variant& v) {
				setCallback(reinterpret_cast<UIDataVariant*>(const_cast<Rml::Variant*>(&v)), setCallbackData);
			};
		}

		return entry->constructor.BindFunc(ToRmlString(name), std::move(rmlGet), std::move(rmlSet));
	}

	bool UIDataModelConstructor::BindFunc(StringView name, FnUIDataGet getCallback, FnUIDataSet setCallback)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !getCallback) return false;

		Rml::DataGetFunc rmlGet = [getCallback = std::move(getCallback)](Rml::Variant& v) {
			getCallback(reinterpret_cast<UIDataVariant*>(&v));
		};

		Rml::DataSetFunc rmlSet = {};
		if (setCallback)
		{
			rmlSet = [setCallback = std::move(setCallback)](const Rml::Variant& v) {
				setCallback(reinterpret_cast<UIDataVariant*>(const_cast<Rml::Variant*>(&v)));
			};
		}

		return entry->constructor.BindFunc(ToRmlString(name), std::move(rmlGet), std::move(rmlSet));
	}

	bool UIDataModelConstructor::BindEventCallback(StringView name,
	                                               FnUIDataEventCallback callback, VoidPtr userData)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !callback) return false;

		return entry->constructor.BindEventCallback(ToRmlString(name),
			[callback, userData](Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList&) {
				callback(reinterpret_cast<UIDataModel*>(ExtractDataModelPtr(handle)), reinterpret_cast<UIEvent*>(&event), userData);
			});
	}

	bool UIDataModelConstructor::BindEventCallback(StringView name, FnUIDataEvent callback)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !callback) return false;

		return entry->constructor.BindEventCallback(ToRmlString(name),
			[callback = std::move(callback)](Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList&) {
				callback(reinterpret_cast<UIDataModel*>(ExtractDataModelPtr(handle)), reinterpret_cast<UIEvent*>(&event));
			});
	}

	bool UIDataModelConstructor::BindVariable(StringView name, f32* ptr)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !ptr) return false;
		return entry->constructor.Bind(ToRmlString(name), ptr);
	}

	bool UIDataModelConstructor::BindVariable(StringView name, i32* ptr)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !ptr) return false;
		return entry->constructor.Bind(ToRmlString(name), ptr);
	}

	bool UIDataModelConstructor::BindVariable(StringView name, bool* ptr)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !ptr) return false;
		return entry->constructor.Bind(ToRmlString(name), ptr);
	}

	bool UIDataModelConstructor::BindScalar(StringView name,
	                                        f32 (*get)(VoidPtr), VoidPtr getData,
	                                        void (*set)(f32, VoidPtr), VoidPtr setData)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !get) return false;

		Rml::DataGetFunc rmlGet = [get, getData](Rml::Variant& v) {
			v = static_cast<float>(get(getData));
		};
		Rml::DataSetFunc rmlSet = {};
		if (set) {
			rmlSet = [set, setData](const Rml::Variant& v) {
				set(static_cast<f32>(v.Get<float>(0.0f)), setData);
			};
		}
		return entry->constructor.BindFunc(ToRmlString(name), std::move(rmlGet), std::move(rmlSet));
	}

	bool UIDataModelConstructor::BindScalar(StringView name,
	                                        i32 (*get)(VoidPtr), VoidPtr getData,
	                                        void (*set)(i32, VoidPtr), VoidPtr setData)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !get) return false;

		Rml::DataGetFunc rmlGet = [get, getData](Rml::Variant& v) {
			v = static_cast<int>(get(getData));
		};
		Rml::DataSetFunc rmlSet = {};
		if (set) {
			rmlSet = [set, setData](const Rml::Variant& v) {
				set(static_cast<i32>(v.Get<int>(0)), setData);
			};
		}
		return entry->constructor.BindFunc(ToRmlString(name), std::move(rmlGet), std::move(rmlSet));
	}

	bool UIDataModelConstructor::BindScalar(StringView name,
	                                        bool (*get)(VoidPtr), VoidPtr getData,
	                                        void (*set)(bool, VoidPtr), VoidPtr setData)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !get) return false;

		Rml::DataGetFunc rmlGet = [get, getData](Rml::Variant& v) {
			v = get(getData);
		};
		Rml::DataSetFunc rmlSet = {};
		if (set) {
			rmlSet = [set, setData](const Rml::Variant& v) {
				set(v.Get<bool>(false), setData);
			};
		}
		return entry->constructor.BindFunc(ToRmlString(name), std::move(rmlGet), std::move(rmlSet));
	}

	bool UIDataModelConstructor::BindScalar(StringView name,
	                                        String (*get)(VoidPtr), VoidPtr getData,
	                                        void (*set)(StringView, VoidPtr), VoidPtr setData)
	{
		auto* entry = reinterpret_cast<UIDataModelConstructorEntry*>(this);
		if (!entry || !get) return false;

		Rml::DataGetFunc rmlGet = [get, getData](Rml::Variant& v) {
			String s = get(getData);
			v = ToRmlString(StringView(s));
		};
		Rml::DataSetFunc rmlSet = {};
		if (set) {
			rmlSet = [set, setData](const Rml::Variant& v) {
				Rml::String s = v.Get<Rml::String>({});
				set(StringView(s.c_str(), s.size()), setData);
			};
		}
		return entry->constructor.BindFunc(ToRmlString(name), std::move(rmlGet), std::move(rmlSet));
	}

	bool UIDataModel::IsVariableDirty(StringView variableName)
	{
		Rml::DataModel* model = reinterpret_cast<Rml::DataModel*>(this);
		if (!model) return false;
		return Rml::DataModelHandle(model).IsVariableDirty(ToRmlString(variableName));
	}

	void UIDataModel::DirtyVariable(StringView variableName)
	{
		Rml::DataModel* model = reinterpret_cast<Rml::DataModel*>(this);
		if (!model) return;
		Rml::DataModelHandle(model).DirtyVariable(ToRmlString(variableName));
	}

	void UIDataModel::DirtyAllVariables()
	{
		Rml::DataModel* model = reinterpret_cast<Rml::DataModel*>(this);
		if (!model) return;
		Rml::DataModelHandle(model).DirtyAllVariables();
	}

	String UIDataVariant::GetString(StringView defaultValue)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return String(defaultValue);
		return FromRmlString(variant->Get<Rml::String>(ToRmlString(defaultValue)));
	}

	void UIDataVariant::SetString(StringView value)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return;
		*variant = ToRmlString(value);
	}

	f32 UIDataVariant::GetFloat(f32 defaultValue)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return defaultValue;
		return variant->Get<float>(defaultValue);
	}

	void UIDataVariant::SetFloat(f32 value)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return;
		*variant = static_cast<float>(value);
	}

	i32 UIDataVariant::GetInt(i32 defaultValue)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return defaultValue;
		return variant->Get<int>(defaultValue);
	}

	void UIDataVariant::SetInt(i32 value)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return;
		*variant = static_cast<int>(value);
	}

	bool UIDataVariant::GetBool(bool defaultValue)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return defaultValue;
		return variant->Get<bool>(defaultValue);
	}

	void UIDataVariant::SetBool(bool value)
	{
		Rml::Variant* variant = reinterpret_cast<Rml::Variant*>(this);
		if (!variant) return;
		*variant = value;
	}

	void RmlUIInit()
	{
		renderInterface = Alloc<RenderInterfaceSkore>();
		systemInterface = Alloc<SystemInterfaceSkore>();
		fontEngine = Alloc<FontEngineSkore>();
		fileInterface = Alloc<FileInterfaceSkore>();

		Rml::SetRenderInterface(renderInterface);
		Rml::SetSystemInterface(systemInterface);
		Rml::SetFontEngineInterface(fontEngine);
		Rml::SetFileInterface(fileInterface);
		Rml::Initialise();

		Event::Bind<OnKeyDown, OnKeyDownEvent>();
		Event::Bind<OnKeyUp, OnKeyUpEvent>();
		Event::Bind<OnTextInput, OnTextInputEvent>();
		Event::Bind<OnMouseMove, OnMouseMoveEvent>();
		Event::Bind<OnMouseButton, OnMouseButtonEvent>();
		Event::Bind<OnMouseScroll, OnMouseScrollEvent>();
	}

	void RmlUIShutdown()
	{
		Event::Unbind<OnKeyDown, OnKeyDownEvent>();
		Event::Unbind<OnKeyUp, OnKeyUpEvent>();
		Event::Unbind<OnTextInput, OnTextInputEvent>();
		Event::Unbind<OnMouseMove, OnMouseMoveEvent>();
		Event::Unbind<OnMouseButton, OnMouseButtonEvent>();
		Event::Unbind<OnMouseScroll, OnMouseScrollEvent>();

		for (DocumentEntry* entry : documents)
		{
			UnregisterDocumentEvents(entry);
			DestroyAndFree(entry);
		}
		documents.Clear();

		{
			std::scoped_lock lock(contextsMutex);
			for (ContextEntry* entry : contexts)
			{
				if (entry->context)
				{
					Rml::RemoveContext(entry->context->GetName());
				}
				DestroyAndFree(entry);
			}
			contexts.Clear();
		}

		Rml::Shutdown();

		if (renderInterface)
		{
			renderInterface->Destroy();
			DestroyAndFree(renderInterface);
			renderInterface = nullptr;
		}
		if (systemInterface)
		{
			DestroyAndFree(systemInterface);
			systemInterface = nullptr;
		}
		if (fontEngine)
		{
			DestroyAndFree(fontEngine);
			fontEngine = nullptr;
		}
		if (fileInterface)
		{
			DestroyAndFree(fileInterface);
			fileInterface = nullptr;
		}
	}

	struct RmlUiRenderPass : RenderPipelinePass
	{
		SK_CLASS(RmlUiRenderPass, RenderPipelinePass);

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::UI;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		bool IsEnabled() override
		{
			Scene* scene = context->GetScene();
			return renderInterface != nullptr && scene != nullptr && IsContextVisible(scene->uiContext) && scene->HasIterable<UIDocument>();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!renderInterface || !scene || !IsContextVisible(scene->uiContext))
			{
				return;
			}

			scene->uiContext->SetDimensions(context->GetOutputSize());
			scene->uiContext->SetDensityIndependentPixelRatio(Platform::GetWindowDPI(Graphics::GetWindow()));
			scene->uiContext->Update();

			renderInterface->BeginFrame(cmd, renderPass, context->GetOutputSize());
			scene->uiContext->Render();
			renderInterface->EndFrame();
		}
	};

	struct RmlUiRenderPipelineModule : RenderPipelineModule
	{
		SK_CLASS(RmlUiRenderPipelineModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(RmlUiRenderPass));
			return setup;
		}
	};

	void RegisterRmlUiRenderModule()
	{
		Reflection::Type<RmlUiRenderPass>();
		Reflection::Type<RmlUiRenderPipelineModule>();
	}
}
