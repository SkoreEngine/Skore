#include "Skore/Graphics/GraphicsResources.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	RID ShaderResource::GetVariant(RID shader, StringView name)
	{
		RID retVariant = {};
		if (ResourceObject shaderObject = Resources::Read(shader))
		{
			shaderObject.IterateSubObjectList(Variants, [&](RID variant)
			{
				if (ResourceObject variantObject = Resources::Read(variant))
				{
					//TODO : break iteration here.
					if (variantObject.GetString(ShaderVariantResource::Name) == name)
					{
						retVariant = variant;
					}
				}
			});
		}
		return retVariant;
	}

	u32 ShaderResource::GetRayHitGroup(RID shader)
	{
		if (!shader) return 0;
		if (ResourceObject shaderObject = Resources::Read(shader))
		{
			return static_cast<u32>(shaderObject.GetUInt(RayHitGroup));
		}
		return 0;
	}

	String ShaderResource::GetVariantName(Span<String> macros)
	{
		if (macros.Empty()) return "Default";

		Array<String> sorted;
		for (const String& m : macros)
		{
			sorted.EmplaceBack(m);
		}

		for (usize i = 0; i < sorted.Size(); i++)
		{
			for (usize j = i + 1; j < sorted.Size(); j++)
			{
				if (sorted[j] < sorted[i])
				{
					String tmp = Traits::Move(sorted[i]);
					sorted[i] = Traits::Move(sorted[j]);
					sorted[j] = Traits::Move(tmp);
				}
			}
		}

		String result;
		for (usize i = 0; i < sorted.Size(); i++)
		{
			if (i > 0) result += "_";
			result += sorted[i];
		}
		return result;
	}

	void FontMetrics::RegisterType(NativeReflectType<FontMetrics>& type)
	{
		type.Field<&FontMetrics::emSize>("emSize");
		type.Field<&FontMetrics::ascenderY>("ascenderY");
		type.Field<&FontMetrics::descenderY>("descenderY");
		type.Field<&FontMetrics::lineHeight>("lineHeight");
		type.Field<&FontMetrics::underlineY>("underlineY");
		type.Field<&FontMetrics::underlineThickness>("underlineThickness");
	}

	void FontAtlasData::RegisterType(NativeReflectType<FontAtlasData>& type)
	{
		type.Field<&FontAtlasData::extent>("extent");
		type.Field<&FontAtlasData::pixels>("pixels");
	}

	void FontKerning::RegisterType(NativeReflectType<FontKerning>& type)
	{
		type.Field<&FontKerning::first>("first");
		type.Field<&FontKerning::second>("second");
		type.Field<&FontKerning::offset>("offset");
	}

	void FontGlyph::RegisterType(NativeReflectType<FontGlyph>& type)
	{
		type.Field<&FontGlyph::codepoint>("codepoint");
		type.Field<&FontGlyph::index>("index");
		type.Field<&FontGlyph::advance>("advance");
		type.Field<&FontGlyph::atlasBounds>("atlasBounds");
		type.Field<&FontGlyph::planeBounds>("planeBounds");
	}

	void FontResourceData::RegisterType(NativeReflectType<FontResourceData>& type)
	{
		type.Field<&FontResourceData::metrics>("metrics");
		type.Field<&FontResourceData::glyphs>("glyphs");
		type.Field<&FontResourceData::kernings>("kernings");
		type.Field<&FontResourceData::atlas>("atlas");
		type.Field<&FontResourceData::maxHeightGlyph>("maxHeightGlyph");
	}

	void CreateGraphicsDefaultValues()
	{
		//RID defaultMaterial = Resources::Create<TextureResource>();
		// ResourceObject defaultMaterialObject = Resources::Write(defaultMaterial);
		// defaultMaterialObject.SetColor(MaterialResource::BaseColor, Color::WHITE);
		// defaultMaterialObject.Commit();
		// Resources::FindType<MaterialResource>()->SetDefaultValue(defaultMaterial);
	}
}