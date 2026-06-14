#include "RmlUI.hpp"
#include "Skore/Scene/Components/UIDocument.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
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
				RenderInterfaceSkore* renderInterface = RmlUiManager::GetRenderInterface();
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
		RenderInterfaceSkore* renderInterface = nullptr;
		SystemInterfaceSkore* systemInterface = nullptr;
		FontEngineSkore*      fontEngine = nullptr;
		FileInterfaceSkore*   fileInterface = nullptr;

		struct ContextEntry
		{
			Rml::Context* context = nullptr;
			Vec2          offset = {0, 0};
			f32           scale = 1.0f;
		};

		Array<ContextEntry> contexts;

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
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessKeyDown(identifier, modifiers);
			}
		}

		void OnKeyUpEvent(Key key)
		{
			Rml::Input::KeyIdentifier identifier = ToRmlKey(key);
			int                       modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessKeyUp(identifier, modifiers);
			}
		}

		void OnTextInputEvent(StringView text)
		{
			Rml::String string(text.Data(), text.Size());
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessTextInput(string);
			}
		}

		void OnMouseMoveEvent(Vec2 position)
		{
			int modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				Vec2 local = (position - entry.offset) * entry.scale;
				entry.context->ProcessMouseMove(static_cast<int>(local.x), static_cast<int>(local.y), modifiers);
			}
		}

		void OnMouseButtonEvent(MouseButton button, bool pressed)
		{
			int index = ToRmlMouseButton(button);
			int modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				if (pressed)
				{
					entry.context->ProcessMouseButtonDown(index, modifiers);
				}
				else
				{
					entry.context->ProcessMouseButtonUp(index, modifiers);
				}
			}
		}

		void OnMouseScrollEvent(Vec2 delta)
		{
			int modifiers = GetRmlModifiers();
			for (const ContextEntry& entry : contexts)
			{
				entry.context->ProcessMouseWheel(Rml::Vector2f(-delta.x, -delta.y), modifiers);
			}
		}
	}

	RenderInterfaceSkore* RmlUiManager::GetRenderInterface()
	{
		return renderInterface;
	}

	void RmlUiManager::RegisterContext(UIContext context)
	{
		if (Rml::Context* rmlContext = context.ToPtr<Rml::Context>())
		{
			contexts.EmplaceBack(ContextEntry{rmlContext});
		}
	}

	void RmlUiManager::UnregisterContext(UIContext context)
	{
		Rml::Context* rmlContext = context.ToPtr<Rml::Context>();
		for (auto it = contexts.begin(); it != contexts.end(); ++it)
		{
			if (it->context == rmlContext)
			{
				contexts.Erase(it);
				break;
			}
		}
	}

	void RmlUiManager::SetInputTransform(Vec2 offset, f32 scale)
	{
		for (ContextEntry& entry : contexts)
		{
			entry.offset = offset;
			entry.scale = scale;
		}
	}

	UIContext RmlUI::CreateContext(StringView name, Extent dimensions)
	{
		Rml::Context* context = Rml::CreateContext(Rml::String(name.Data(), name.Size()),
		                                           Rml::Vector2i(static_cast<int>(dimensions.width), static_cast<int>(dimensions.height)));
		return UIContext(context);
	}

	void RmlUI::RemoveContext(UIContext context)
	{
		if (Rml::Context* rmlContext = context.ToPtr<Rml::Context>())
		{
			Rml::RemoveContext(rmlContext->GetName());
		}
	}

	void RmlUI::SetDimensions(UIContext context, Extent dimensions)
	{
		if (Rml::Context* rmlContext = context.ToPtr<Rml::Context>())
		{
			rmlContext->SetDimensions(Rml::Vector2i(static_cast<int>(dimensions.width), static_cast<int>(dimensions.height)));
		}
	}

	void RmlUI::SetDensityIndependentPixelRatio(UIContext context, f32 ratio)
	{
		if (Rml::Context* rmlContext = context.ToPtr<Rml::Context>())
		{
			rmlContext->SetDensityIndependentPixelRatio(ratio);
		}
	}

	void RmlUI::Update(UIContext context)
	{
		if (Rml::Context* rmlContext = context.ToPtr<Rml::Context>())
		{
			rmlContext->Update();
		}
	}

	void RmlUI::Render(UIContext context)
	{
		if (Rml::Context* rmlContext = context.ToPtr<Rml::Context>())
		{
			rmlContext->Render();
		}
	}

	UIElementDocument RmlUI::LoadDocumentFromMemory(UIContext context, StringView content)
	{
		Rml::Context* rmlContext = context.ToPtr<Rml::Context>();
		if (!rmlContext)
		{
			return {};
		}
		return UIElementDocument(rmlContext->LoadDocumentFromMemory(Rml::String(content.Data(), content.Size())));
	}

	void RmlUI::UnloadDocument(UIContext context, UIElementDocument document)
	{
		Rml::Context* rmlContext = context.ToPtr<Rml::Context>();
		if (rmlContext && document)
		{
			rmlContext->UnloadDocument(document.ToPtr<Rml::ElementDocument>());
		}
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

	void RmlUI::ShowDocument(UIElementDocument document, UIModalFlag modalFlag, UIFocusFlag focusFlag, UIScrollFlag scrollFlag)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->Show(ToRmlModalFlag(modalFlag), ToRmlFocusFlag(focusFlag), ToRmlScrollFlag(scrollFlag));
		}
	}

	void RmlUI::HideDocument(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->Hide();
		}
	}

	void RmlUI::CloseDocument(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->Close();
		}
	}

	void RmlUI::PullDocumentToFront(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->PullToFront();
		}
	}

	void RmlUI::PushDocumentToBack(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->PushToBack();
		}
	}

	void RmlUI::SetDocumentTitle(UIElementDocument document, StringView title)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->SetTitle(Rml::String(title.Data(), title.Size()));
		}
	}

	String RmlUI::GetDocumentTitle(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			const Rml::String& title = element->GetTitle();
			return String(title.c_str(), title.size());
		}
		return {};
	}

	String RmlUI::GetDocumentSourceURL(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			const Rml::String& url = element->GetSourceURL();
			return String(url.c_str(), url.size());
		}
		return {};
	}

	bool RmlUI::IsDocumentModal(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			return element->IsModal();
		}
		return false;
	}

	void RmlUI::ReloadDocumentStyleSheet(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->ReloadStyleSheet();
		}
	}

	void RmlUI::UpdateDocument(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			element->UpdateDocument();
		}
	}

	UIContext RmlUI::GetDocumentContext(UIElementDocument document)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			return UIContext(element->GetContext());
		}
		return {};
	}

	UIElement RmlUI::FindNextTabElement(UIElementDocument document, UIElement currentElement, bool forward, bool wrapAround)
	{
		if (Rml::ElementDocument* element = document.ToPtr<Rml::ElementDocument>())
		{
			return UIElement(element->FindNextTabElement(currentElement.ToPtr<Rml::Element>(), forward, wrapAround));
		}
		return {};
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

		Array<UIElement> ToElementArray(const Rml::ElementList& list)
		{
			Array<UIElement> result;
			result.Reserve(list.size());
			for (Rml::Element* element : list)
			{
				result.EmplaceBack(UIElement(element));
			}
			return result;
		}
	}

	UIElement RmlUI::CreateElement(UIElementDocument document, StringView name)
	{
		if (Rml::ElementDocument* doc = document.ToPtr<Rml::ElementDocument>())
		{
			return UIElement(doc->CreateElement(ToRmlString(name)).release());
		}
		return {};
	}

	UIElement RmlUI::CreateTextNode(UIElementDocument document, StringView text)
	{
		if (Rml::ElementDocument* doc = document.ToPtr<Rml::ElementDocument>())
		{
			return UIElement(doc->CreateTextNode(ToRmlString(text)).release());
		}
		return {};
	}

	UIElement RmlUI::CloneElement(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->Clone().release());
		}
		return {};
	}

	void RmlUI::DestroyElement(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			Rml::ElementPtr owned(el);
		}
	}

	UIElement RmlUI::AppendChild(UIElement parent, UIElement child)
	{
		Rml::Element* parentElement = parent.ToPtr<Rml::Element>();
		Rml::Element* childElement = child.ToPtr<Rml::Element>();
		if (parentElement && childElement)
		{
			return UIElement(parentElement->AppendChild(Rml::ElementPtr(childElement)));
		}
		return {};
	}

	UIElement RmlUI::InsertBefore(UIElement parent, UIElement child, UIElement adjacentElement)
	{
		Rml::Element* parentElement = parent.ToPtr<Rml::Element>();
		Rml::Element* childElement = child.ToPtr<Rml::Element>();
		if (parentElement && childElement)
		{
			return UIElement(parentElement->InsertBefore(Rml::ElementPtr(childElement), adjacentElement.ToPtr<Rml::Element>()));
		}
		return {};
	}

	UIElement RmlUI::RemoveChild(UIElement parent, UIElement child)
	{
		Rml::Element* parentElement = parent.ToPtr<Rml::Element>();
		Rml::Element* childElement = child.ToPtr<Rml::Element>();
		if (parentElement && childElement)
		{
			return UIElement(parentElement->RemoveChild(childElement).release());
		}
		return {};
	}

	bool RmlUI::HasChildNodes(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->HasChildNodes();
		}
		return false;
	}

	void RmlUI::SetElementClass(UIElement element, StringView className, bool activate)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetClass(ToRmlString(className), activate);
		}
	}

	bool RmlUI::IsElementClassSet(UIElement element, StringView className)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->IsClassSet(ToRmlString(className));
		}
		return false;
	}

	void RmlUI::SetElementClassNames(UIElement element, StringView classNames)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetClassNames(ToRmlString(classNames));
		}
	}

	String RmlUI::GetElementClassNames(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return FromRmlString(el->GetClassNames());
		}
		return {};
	}

	void RmlUI::SetElementPseudoClass(UIElement element, StringView pseudoClass, bool activate)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetPseudoClass(ToRmlString(pseudoClass), activate);
		}
	}

	bool RmlUI::IsElementPseudoClassSet(UIElement element, StringView pseudoClass)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->IsPseudoClassSet(ToRmlString(pseudoClass));
		}
		return false;
	}

	bool RmlUI::SetElementProperty(UIElement element, StringView name, StringView value)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->SetProperty(ToRmlString(name), ToRmlString(value));
		}
		return false;
	}

	void RmlUI::RemoveElementProperty(UIElement element, StringView name)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->RemoveProperty(ToRmlString(name));
		}
	}

	void RmlUI::SetElementAttribute(UIElement element, StringView name, StringView value)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetAttribute(ToRmlString(name), ToRmlString(value));
		}
	}

	String RmlUI::GetElementAttribute(UIElement element, StringView name, StringView defaultValue)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return FromRmlString(el->GetAttribute<Rml::String>(ToRmlString(name), ToRmlString(defaultValue)));
		}
		return FromRmlString(ToRmlString(defaultValue));
	}

	bool RmlUI::HasElementAttribute(UIElement element, StringView name)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->HasAttribute(ToRmlString(name));
		}
		return false;
	}

	void RmlUI::RemoveElementAttribute(UIElement element, StringView name)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->RemoveAttribute(ToRmlString(name));
		}
	}

	String RmlUI::GetElementTagName(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return FromRmlString(el->GetTagName());
		}
		return {};
	}

	String RmlUI::GetElementId(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return FromRmlString(el->GetId());
		}
		return {};
	}

	void RmlUI::SetElementId(UIElement element, StringView id)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetId(ToRmlString(id));
		}
	}

	String RmlUI::GetElementInnerRML(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return FromRmlString(el->GetInnerRML());
		}
		return {};
	}

	void RmlUI::SetElementInnerRML(UIElement element, StringView rml)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetInnerRML(ToRmlString(rml));
		}
	}

	bool RmlUI::IsElementVisible(UIElement element, bool includeAncestors)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->IsVisible(includeAncestors);
		}
		return false;
	}

	f32 RmlUI::GetElementAbsoluteLeft(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetAbsoluteLeft();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementAbsoluteTop(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetAbsoluteTop();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementClientWidth(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetClientWidth();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementClientHeight(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetClientHeight();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementOffsetWidth(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetOffsetWidth();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementOffsetHeight(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetOffsetHeight();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementScrollLeft(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetScrollLeft();
		}
		return 0.0f;
	}

	void RmlUI::SetElementScrollLeft(UIElement element, f32 scrollLeft)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetScrollLeft(scrollLeft);
		}
	}

	f32 RmlUI::GetElementScrollTop(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetScrollTop();
		}
		return 0.0f;
	}

	void RmlUI::SetElementScrollTop(UIElement element, f32 scrollTop)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->SetScrollTop(scrollTop);
		}
	}

	f32 RmlUI::GetElementScrollWidth(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetScrollWidth();
		}
		return 0.0f;
	}

	f32 RmlUI::GetElementScrollHeight(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetScrollHeight();
		}
		return 0.0f;
	}

	bool RmlUI::FocusElement(UIElement element, bool focusVisible)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->Focus(focusVisible);
		}
		return false;
	}

	void RmlUI::BlurElement(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->Blur();
		}
	}

	void RmlUI::ClickElement(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->Click();
		}
	}

	void RmlUI::ScrollElementIntoView(UIElement element, bool alignWithTop)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			el->ScrollIntoView(alignWithTop);
		}
	}

	UIElement RmlUI::GetElementParentNode(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetParentNode());
		}
		return {};
	}

	UIElement RmlUI::GetElementNextSibling(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetNextSibling());
		}
		return {};
	}

	UIElement RmlUI::GetElementPreviousSibling(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetPreviousSibling());
		}
		return {};
	}

	UIElement RmlUI::GetElementFirstChild(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetFirstChild());
		}
		return {};
	}

	UIElement RmlUI::GetElementLastChild(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetLastChild());
		}
		return {};
	}

	UIElement RmlUI::GetElementChild(UIElement element, i32 index)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetChild(index));
		}
		return {};
	}

	i32 RmlUI::GetElementNumChildren(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->GetNumChildren();
		}
		return 0;
	}

	UIElementDocument RmlUI::GetElementOwnerDocument(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElementDocument(el->GetOwnerDocument());
		}
		return {};
	}

	UIContext RmlUI::GetElementContext(UIElement element)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIContext(el->GetContext());
		}
		return {};
	}

	UIElement RmlUI::GetElementById(UIElement element, StringView id)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->GetElementById(ToRmlString(id)));
		}
		return {};
	}

	UIElement RmlUI::QuerySelector(UIElement element, StringView selector)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->QuerySelector(ToRmlString(selector)));
		}
		return {};
	}

	Array<UIElement> RmlUI::QuerySelectorAll(UIElement element, StringView selector)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			Rml::ElementList list;
			el->QuerySelectorAll(list, ToRmlString(selector));
			return ToElementArray(list);
		}
		return {};
	}

	Array<UIElement> RmlUI::GetElementsByTagName(UIElement element, StringView tag)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			Rml::ElementList list;
			el->GetElementsByTagName(list, ToRmlString(tag));
			return ToElementArray(list);
		}
		return {};
	}

	Array<UIElement> RmlUI::GetElementsByClassName(UIElement element, StringView className)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			Rml::ElementList list;
			el->GetElementsByClassName(list, ToRmlString(className));
			return ToElementArray(list);
		}
		return {};
	}

	UIElement RmlUI::Closest(UIElement element, StringView selectors)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return UIElement(el->Closest(ToRmlString(selectors)));
		}
		return {};
	}

	bool RmlUI::ElementMatches(UIElement element, StringView selector)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->Matches(ToRmlString(selector));
		}
		return false;
	}

	bool RmlUI::ElementContains(UIElement element, UIElement other)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->Contains(other.ToPtr<Rml::Element>());
		}
		return false;
	}

	namespace
	{
		class EventListenerSkore : public Rml::EventListener
		{
		public:
			EventListenerSkore(FnUIEventCallback callback, VoidPtr userData) : callback(callback), userData(userData) {}

			void ProcessEvent(Rml::Event& event) override
			{
				if (callback)
				{
					callback(UIEvent(&event), userData);
				}
			}

			void OnDetach(Rml::Element*) override
			{
				DestroyAndFree(this);
			}

		private:
			FnUIEventCallback callback = nullptr;
			VoidPtr           userData = nullptr;
		};
	}

	UIEventListener RmlUI::AddEventListener(UIElement element, StringView event, FnUIEventCallback callback, VoidPtr userData, bool inCapturePhase)
	{
		Rml::Element* el = element.ToPtr<Rml::Element>();
		if (!el || !callback)
		{
			return {};
		}
		EventListenerSkore* listener = Alloc<EventListenerSkore>(callback, userData);
		el->AddEventListener(ToRmlString(event), listener, inCapturePhase);
		return UIEventListener(listener);
	}

	void RmlUI::RemoveEventListener(UIElement element, StringView event, UIEventListener listener, bool inCapturePhase)
	{
		Rml::Element* el = element.ToPtr<Rml::Element>();
		if (el && listener)
		{
			el->RemoveEventListener(ToRmlString(event), listener.ToPtr<Rml::EventListener>(), inCapturePhase);
		}
	}

	bool RmlUI::DispatchEvent(UIElement element, StringView type)
	{
		if (Rml::Element* el = element.ToPtr<Rml::Element>())
		{
			return el->DispatchEvent(ToRmlString(type), Rml::Dictionary());
		}
		return false;
	}

	String RmlUI::GetEventType(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return FromRmlString(ev->GetType());
		}
		return {};
	}

	UIElement RmlUI::GetEventTargetElement(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return UIElement(ev->GetTargetElement());
		}
		return {};
	}

	UIElement RmlUI::GetEventCurrentElement(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return UIElement(ev->GetCurrentElement());
		}
		return {};
	}

	UIEventPhase RmlUI::GetEventPhase(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return static_cast<UIEventPhase>(ev->GetPhase());
		}
		return UIEventPhase::None;
	}

	bool RmlUI::IsEventInterruptible(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return ev->IsInterruptible();
		}
		return false;
	}

	bool RmlUI::IsEventPropagating(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return ev->IsPropagating();
		}
		return false;
	}

	void RmlUI::StopEventPropagation(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			ev->StopPropagation();
		}
	}

	void RmlUI::StopEventImmediatePropagation(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			ev->StopImmediatePropagation();
		}
	}

	Vec2 RmlUI::GetEventUnprojectedMouseScreenPos(UIEvent event)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			Rml::Vector2f pos = ev->GetUnprojectedMouseScreenPos();
			return Vec2(pos.x, pos.y);
		}
		return Vec2(0.0f, 0.0f);
	}

	String RmlUI::GetEventParameterString(UIEvent event, StringView key, StringView defaultValue)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return FromRmlString(ev->GetParameter<Rml::String>(ToRmlString(key), ToRmlString(defaultValue)));
		}
		return FromRmlString(ToRmlString(defaultValue));
	}

	f32 RmlUI::GetEventParameterFloat(UIEvent event, StringView key, f32 defaultValue)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return ev->GetParameter<float>(ToRmlString(key), defaultValue);
		}
		return defaultValue;
	}

	i32 RmlUI::GetEventParameterInt(UIEvent event, StringView key, i32 defaultValue)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return ev->GetParameter<int>(ToRmlString(key), defaultValue);
		}
		return defaultValue;
	}

	bool RmlUI::GetEventParameterBool(UIEvent event, StringView key, bool defaultValue)
	{
		if (Rml::Event* ev = event.ToPtr<Rml::Event>())
		{
			return ev->GetParameter<bool>(ToRmlString(key), defaultValue);
		}
		return defaultValue;
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

		struct DocumentEntry
		{
			UIContext         uiContext = {};
			UIElementDocument element = {};
			RID               loadedDocument = {};
			u64               lastSeenFrame = 0;
		};

		HashMap<UIDocument*, DocumentEntry> entries;
		u64                                 contextCounter = 0;

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
			return RmlUiManager::GetRenderInterface() != nullptr && scene != nullptr && scene->HasIterable<UIDocument>();
		}

		void DestroyEntry(DocumentEntry& entry)
		{
			if (!entry.uiContext)
			{
				return;
			}
			if (entry.element)
			{
				RmlUI::UnloadDocument(entry.uiContext, entry.element);
			}
			RmlUiManager::UnregisterContext(entry.uiContext);
			RmlUI::RemoveContext(entry.uiContext);
		}

		void Destroy() override
		{
			for (auto it = entries.begin(); it != entries.end(); ++it)
			{
				DestroyEntry(it->second);
			}
			entries.Clear();
		}

		void SyncDocument(DocumentEntry& entry, UIDocument* document, Extent size)
		{
			if (!entry.uiContext)
			{
				String name = "UIDocument_" + ToString(contextCounter++);
				entry.uiContext = RmlUI::CreateContext(name, size);
				if (!entry.uiContext)
				{
					return;
				}
				RmlUiManager::RegisterContext(entry.uiContext);
			}

			RID documentRid = document->GetDocument();
			if (documentRid == entry.loadedDocument)
			{
				return;
			}

			if (entry.element)
			{
				RmlUI::UnloadDocument(entry.uiContext, entry.element);
				entry.element = {};
			}

			entry.loadedDocument = documentRid;

			if (ResourceObject object = Resources::Read(documentRid))
			{
				String content = object.GetString(UIDocumentResource::Content);
				if (!content.Empty())
				{
					entry.element = RmlUI::LoadDocumentFromMemory(entry.uiContext, content);
					if (entry.element)
					{
						RmlUI::ShowDocument(entry.element);
					}
				}
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			RenderInterfaceSkore* renderInterface = RmlUiManager::GetRenderInterface();
			if (!renderInterface || !scene)
			{
				return;
			}

			const Extent size = context->GetOutputSize();
			const u64    frame = App::Frame();
			const f32    dpi = Platform::GetWindowDPI(Graphics::GetWindow());

			scene->Iterate<UIDocument>([&](UIDocument* document)
			{
				DocumentEntry& entry = entries[document];

				SyncDocument(entry, document, size);
				if (!entry.uiContext)
				{
					return;
				}

				entry.lastSeenFrame = frame;

				RmlUI::SetDimensions(entry.uiContext, size);
				RmlUI::SetDensityIndependentPixelRatio(entry.uiContext, dpi);
				RmlUI::Update(entry.uiContext);

				renderInterface->BeginFrame(cmd, renderPass, size);
				RmlUI::Render(entry.uiContext);
				renderInterface->EndFrame();
			});

			Array<UIDocument*> stale;
			for (auto it = entries.begin(); it != entries.end(); ++it)
			{
				if (it->second.lastSeenFrame != frame)
				{
					DestroyEntry(it->second);
					stale.EmplaceBack(it->first);
				}
			}
			for (UIDocument* document : stale)
			{
				entries.Erase(document);
			}
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
