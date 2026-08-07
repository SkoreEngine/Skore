#include "Skore/Graphics/GraphicsResources.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	RID ShaderResource::GetVariant(RID shader, StringView name)
	{
		return GetVariant(shader, RID{}, name);
	}

	RID ShaderResource::GetVariant(RID shader, RID material, StringView name)
	{
		if (!shader) return {};

		constexpr u32 maxInstanceParentDepth = 16;
		RID           graph = material;
		for (u32 depth = 0; graph && depth < maxInstanceParentDepth; ++depth)
		{
			ResourceObject materialObject = Resources::Read(graph);
			if (!materialObject)
			{
				graph = {};
				break;
			}
			if (materialObject.GetEnum<MaterialGraphResource::MaterialKind>(MaterialGraphResource::Kind) != MaterialGraphResource::MaterialKind::Instance)
			{
				break;
			}
			graph = materialObject.GetReference(MaterialGraphResource::Parent);
		}

		RID retVariant = {};
		if (ResourceObject shaderObject = Resources::Read(shader))
		{
			shaderObject.IterateSubObjectList(Variants, [&](RID variant)
			{
				if (retVariant) return;
				if (ResourceObject variantObject = Resources::Read(variant))
				{
					if (variantObject.GetReference(ShaderVariantResource::Material) == graph
						&& variantObject.GetString(ShaderVariantResource::Name) == name)
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

	bool ShaderResource::IsMaterialShader(RID shader)
	{
		if (!shader) return false;
		if (ResourceObject shaderObject = Resources::Read(shader))
		{
			return shaderObject.GetBool(IsMaterial);
		}
		return false;
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

	namespace
	{
		bool ParamKindFromTypeId(StringView typeId, MaterialParamKind& kind)
		{
			if (typeId == "param_float" || typeId == "param_int" || typeId == "param_bool" || typeId == "param_channel")
			{
				kind = MaterialParamKind::Float;
				return true;
			}
			if (typeId == "param_vec2")
			{
				kind = MaterialParamKind::Vec2;
				return true;
			}
			if (typeId == "param_color" || typeId == "param_vec3")
			{
				kind = MaterialParamKind::Vec3;
				return true;
			}
			if (typeId == "param_vec4")
			{
				kind = MaterialParamKind::Vec4;
				return true;
			}
			if (typeId == "param_texture2d")
			{
				kind = MaterialParamKind::Texture;
				return true;
			}
			return false;
		}

		u32 ParamKindSize(MaterialParamKind kind)
		{
			switch (kind)
			{
				case MaterialParamKind::Float:   return 4;
				case MaterialParamKind::Vec2:    return 8;
				case MaterialParamKind::Vec3:    return 12;
				case MaterialParamKind::Vec4:    return 16;
				case MaterialParamKind::Texture: return 8; //i32 bindless index + u32 sampler index
			}
			return 4;
		}
	}

	MaterialParamLayout MaterialParamLayout::Build(RID material)
	{
		MaterialParamLayout layout;

		RID graph = material;
		for (u32 depth = 0; graph && depth < 16; ++depth)
		{
			ResourceObject obj = Resources::Read(graph);
			if (!obj)
			{
				graph = {};
				break;
			}
			if (obj.GetEnum<MaterialGraphResource::MaterialKind>(MaterialGraphResource::Kind) != MaterialGraphResource::MaterialKind::Instance)
			{
				break;
			}
			graph = obj.GetReference(MaterialGraphResource::Parent);
		}

		layout.owningGraph = graph;
		if (!graph)
		{
			return layout;
		}

		ResourceObject graphObj = Resources::Read(graph);
		if (!graphObj)
		{
			return layout;
		}

		struct NamedSlot
		{
			u32               offset;
			MaterialParamKind kind;
		};
		HashMap<String, NamedSlot> nameOffsets;
		u32                        offset = 0;

		for (RID node : graphObj.GetSubObjectList(MaterialGraphResource::Nodes))
		{
			ResourceObject nodeObj = Resources::Read(node);
			if (!nodeObj)
			{
				continue;
			}

			StringView typeId = nodeObj.GetString(MaterialGraphNodeResource::Type);

			MaterialParamKind kind;
			bool              isParam = ParamKindFromTypeId(typeId, kind);
			bool              isFixedTexture = !isParam && (typeId == "sample_texture" || typeId == "normal_map");

			if (!isParam && !isFixedTexture)
			{
				continue;
			}

			if (isParam)
			{
				String name = String{nodeObj.GetString(MaterialGraphNodeResource::ParameterName)};
				if (name.Empty())
				{
					continue;
				}

				//same-kind nodes with the same name intentionally share one slot; a kind mismatch gets its
				//own slot so the packed layout never reads past a smaller value (editor validation flags it)
				if (auto it = nameOffsets.Find(name); it != nameOffsets.end() && it->second.kind == kind)
				{
					layout.nodeOffsets.Insert(node.id, it->second.offset);
					continue;
				}

				layout.nodeOffsets.Insert(node.id, offset);
				if (nameOffsets.Find(name) == nameOffsets.end())
				{
					nameOffsets.Insert(name, NamedSlot{offset, kind});
				}
				layout.entries.EmplaceBack(MaterialParamEntry{Traits::Move(name), node, kind, offset});
				offset += ParamKindSize(kind);
			}
			else
			{
				layout.nodeOffsets.Insert(node.id, offset);
				layout.entries.EmplaceBack(MaterialParamEntry{String{}, node, MaterialParamKind::Texture, offset});
				offset += ParamKindSize(MaterialParamKind::Texture);
			}
		}

		layout.size = offset;
		return layout;
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

}