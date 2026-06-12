#include "FontEngineSkore.hpp"

#include <cmath>
#include <utility>

#include "RmlUiManager.hpp"
#include "RenderInterfaceSkore.hpp"

#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Resource/Resources.hpp"

#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/Texture.h>
#include <RmlUi/Core/StringUtilities.h>

#include "Skore/Core/StringUtils.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore.RmlUi");

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

	FontEngineSkore::~FontEngineSkore()
	{
		ReleaseFontResources();
	}

	void FontEngineSkore::RegisterFont(StringView family, RID fontResource)
	{
		registeredFonts.Insert(String(family), fontResource);
	}

	void FontEngineSkore::Shutdown()
	{
		ReleaseFontResources();
	}

	Rml::FontFaceHandle FontEngineSkore::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size)
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

	const Rml::FontMetrics& FontEngineSkore::GetFontMetrics(Rml::FontFaceHandle handle)
	{
		return reinterpret_cast<SkoreFontFace*>(handle)->metrics;
	}

	int FontEngineSkore::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& text_shaping_context, Rml::Character prior_character)
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

	int FontEngineSkore::GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle face_handle, Rml::FontEffectsHandle font_effects_handle, Rml::StringView string,
	                                    Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& text_shaping_context,
	                                    Rml::TexturedMeshList& mesh_list)
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

	int FontEngineSkore::GetVersion(Rml::FontFaceHandle handle)
	{
		return 1;
	}

	void FontEngineSkore::ReleaseFontResources()
	{
		for (SkoreFontFace* face : faces)
		{
			DestroyAndFree(face);
		}
		faces.Clear();
	}
}
