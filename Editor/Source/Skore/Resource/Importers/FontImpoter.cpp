#include "msdf-atlas-gen.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

#define DEFAULT_ANGLE_THRESHOLD 3.0
#define LCG_MULTIPLIER 6364136223846793005ull


namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::FontImporter");

	template <typename T, typename S, int N, msdf_atlas::GeneratorFunction<S, N> GenFunc>
	static void CreateAtlas(const std::vector<msdf_atlas::GlyphGeometry>& glyphs, uint32_t width, uint32_t height, FontResourceData& fontData)
	{
		msdf_atlas::GeneratorAttributes attributes;
		attributes.config.overlapSupport = true;
		attributes.scanlinePass = true;

		msdf_atlas::ImmediateAtlasGenerator<S, N, GenFunc, msdf_atlas::BitmapAtlasStorage<T, N>> generator(width, height);
		generator.setAttributes(attributes);
		generator.setThreadCount(8);
		generator.generate(glyphs.data(), (int)glyphs.size());

		msdfgen::BitmapConstRef<T, N> bitmap = (msdfgen::BitmapConstRef<T, N>)generator.atlasStorage();

		fontData.atlas.extent = Vec2{static_cast<f32>(bitmap.width), static_cast<f32>(bitmap.height)};
		fontData.atlas.pixels = Span((u8*)bitmap.pixels, bitmap.width * bitmap.height * N * sizeof(T));
	}

	struct FontImporter : ResourceAssetImporter
	{
		SK_CLASS(FontImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".ttf", ".otf"};
		}

		bool ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) override
		{
			msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
			msdfgen::FontHandle*     font = msdfgen::loadFont(ft, path.CStr());

			if (!font)
			{
				logger.Error("Failed to load font {}", path);
				return {};
			}

			struct CharsetRange
			{
				u32 begin, end;
			};

			// From imgui_draw.cpp
			static const CharsetRange charsetRanges[] = {{0x0020, 0x00FF}};

			msdf_atlas::Charset charset;
			for (CharsetRange range : charsetRanges)
			{
				for (u32 c = range.begin; c <= range.end; c++)
				{
					charset.add(c);
				}
			}

			double fontScale = 1.0;

			std::vector<msdf_atlas::GlyphGeometry> glyphs;
			msdf_atlas::FontGeometry fontGeometry = msdf_atlas::FontGeometry(&glyphs);

			i32 glyphsLoaded = fontGeometry.loadCharset(font, fontScale, charset);

			msdfgen::FontMetrics msdfgenMetrics = fontGeometry.getMetrics();

			FontResourceData data;
			data.metrics.emSize = msdfgenMetrics.emSize;
			data.metrics.ascenderY = msdfgenMetrics.ascenderY;
			data.metrics.descenderY = msdfgenMetrics.descenderY;
			data.metrics.lineHeight = msdfgenMetrics.lineHeight;
			data.metrics.underlineY = msdfgenMetrics.underlineY;
			data.metrics.underlineThickness = msdfgenMetrics.underlineThickness;

			logger.Debug("Loaded {} glyphs from font (out of {})", glyphsLoaded, charset.size());

			f64 emSize = 40.0;

			msdf_atlas::TightAtlasPacker atlasPacker;
			atlasPacker.setPixelRange(2.0);
			atlasPacker.setMiterLimit(1.0);
			//atlasPacker.setPadding(0);
			atlasPacker.setScale(emSize);
			atlasPacker.pack(glyphs.data(), (int)glyphs.size());

			int width, height;
			atlasPacker.getDimensions(width, height);

			u64 coloringSeed = 0;
			u64 glyphSeed = coloringSeed;

			for (const auto& it : fontGeometry.getKerning())
			{
				FontKerning kerning;
				kerning.first = it.first.first;
				kerning.second = it.first.second;
				kerning.offset = it.second;
				data.kernings.EmplaceBack(kerning);
			}

			for (msdf_atlas::GlyphGeometry& glyph : glyphs)
			{
				glyphSeed *= LCG_MULTIPLIER;
				glyph.edgeColoring(msdfgen::edgeColoringInkTrap, DEFAULT_ANGLE_THRESHOLD, glyphSeed);

				Vec4D quadAtlasBounds;
				Vec4D quadPlaneBounds;

				glyph.getQuadAtlasBounds(quadAtlasBounds.x, quadAtlasBounds.y, quadAtlasBounds.z, quadAtlasBounds.w);
				glyph.getQuadPlaneBounds(quadPlaneBounds.x, quadPlaneBounds.y, quadPlaneBounds.z, quadPlaneBounds.w);

				RID glyphRID = Resources::Create<FontGlyph>(UUID::RandomUUID(), scope);
				ResourceObject glyphObject = Resources::Write(glyphRID);

				FontGlyph fontGlyph;
				fontGlyph.codepoint = glyph.getCodepoint();
				fontGlyph.index = glyph.getIndex();
				fontGlyph.advance = glyph.getAdvance();
				fontGlyph.atlasBounds = quadAtlasBounds;
				fontGlyph.planeBounds = quadPlaneBounds;
				data.glyphs.EmplaceBack(fontGlyph);

				data.maxHeightGlyph = Math::Max(data.maxHeightGlyph, static_cast<f32>(quadPlaneBounds.w));
			}

			CreateAtlas<float, float, 4, msdf_atlas::mtsdfGenerator>(glyphs, width, height, data);

			BinaryArchiveWriter writer;
			writer.BeginMap("data");
			data.Serialize(writer);
			writer.EndMap();

			Span<u8> writerData = writer.GetData();
			usize maxSize = Compression::GetMaxCompressedBufferSize(writerData.Size(), CompressionMode::ZSTD);
			Array<u8> compressedData(maxSize);
			usize size = Compression::Compress(compressedData.Data(), maxSize, writerData.Data(), writerData.Size(), CompressionMode::ZSTD);

			RID fontRID = ResourceAssets::CreateImportedAsset(directory, TypeInfo<FontResource>::ID(), Path::Name(path), scope, path);

			ResourceObject fontObject = Resources::Write(fontRID);
			fontObject.SetString(FontResource::Name, Path::Name(path));
			fontObject.SetUInt(FontResource::FontDataUncompressedSize, writerData.Size());
			fontObject.SetBlob(FontResource::FontData, Span{compressedData.Data(), size});
			fontObject.Commit(scope);

			msdfgen::destroyFont(font);
			msdfgen::deinitializeFreetype(ft);

			return true;
		}
	};


	void RegisterFontImporter()
	{
		Reflection::Type<FontImporter>();
	}
}
