#include "MaterialNode.hpp"

#include "Skore/Core/Traits.hpp"

#include <cstdio>

namespace Skore
{
	namespace
	{
		String Flt(f32 v)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%g", v);
			return String{buf};
		}

		void CodegenConstantFloat(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, Flt(ctx.value.x));
		}

		void CodegenConstantColor(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, MaterialLiteralExpr(MaterialDataType::Vec3, ctx.value));
		}

		void CodegenConstantVec2(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, MaterialLiteralExpr(MaterialDataType::Vec2, ctx.value));
		}

		void CodegenTexCoord(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, "input.texCoord");
		}

		void CodegenMultiply(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " * " + ctx.Input(1) + ")");
		}

		void CodegenAdd(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " + " + ctx.Input(1) + ")");
		}

		void CodegenLerp(MaterialCodegenContext& ctx)
		{
			ctx.SetOutput(0, String{"lerp("} + ctx.Input(0) + ", " + ctx.Input(1) + ", " + ctx.Input(2) + ")");
		}

		Array<MaterialNodeDef> BuildDefs()
		{
			Array<MaterialNodeDef> defs;

			//--- Output / master node ----------------------------------------------------
			{
				MaterialNodeDef def;
				def.typeId = "output";
				def.displayName = "Material Output";
				def.category = "";
				def.headerColor = {200, 120, 80};
				def.isOutput = true;
				def.inputs.EmplaceBack(MaterialNodePin{"Base Color", MaterialDataType::Vec3, Vec4{0.8f, 0.8f, 0.8f, 1.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"Metallic", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"Roughness", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"Emissive", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f}});
				defs.EmplaceBack(Traits::Move(def));
			}

			//--- Constants ---------------------------------------------------------------
			{
				MaterialNodeDef def;
				def.typeId = "constant_float";
				def.displayName = "Float";
				def.category = "Constants";
				def.headerColor = {120, 170, 90};
				def.valueKind = MaterialNodeValueKind::Float;
				def.defaultValue = Vec4{1.0f, 0.0f, 0.0f, 0.0f};
				def.outputs.EmplaceBack(MaterialNodePin{"Out", MaterialDataType::Float});
				def.codegen = CodegenConstantFloat;
				defs.EmplaceBack(Traits::Move(def));
			}
			{
				MaterialNodeDef def;
				def.typeId = "constant_color";
				def.displayName = "Color";
				def.category = "Constants";
				def.headerColor = {120, 170, 90};
				def.valueKind = MaterialNodeValueKind::Color;
				def.defaultValue = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
				def.outputs.EmplaceBack(MaterialNodePin{"Out", MaterialDataType::Vec3});
				def.codegen = CodegenConstantColor;
				defs.EmplaceBack(Traits::Move(def));
			}
			{
				MaterialNodeDef def;
				def.typeId = "constant_vec2";
				def.displayName = "Vector2";
				def.category = "Constants";
				def.headerColor = {120, 170, 90};
				def.valueKind = MaterialNodeValueKind::Vec2;
				def.defaultValue = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
				def.outputs.EmplaceBack(MaterialNodePin{"Out", MaterialDataType::Vec2});
				def.codegen = CodegenConstantVec2;
				defs.EmplaceBack(Traits::Move(def));
			}

			//--- Inputs ------------------------------------------------------------------
			{
				MaterialNodeDef def;
				def.typeId = "tex_coord";
				def.displayName = "Texture Coordinate";
				def.category = "Inputs";
				def.headerColor = {90, 150, 180};
				def.outputs.EmplaceBack(MaterialNodePin{"UV", MaterialDataType::Vec2});
				def.codegen = CodegenTexCoord;
				defs.EmplaceBack(Traits::Move(def));
			}

			//--- Math --------------------------------------------------------------------
			{
				MaterialNodeDef def;
				def.typeId = "multiply";
				def.displayName = "Multiply";
				def.category = "Math";
				def.headerColor = {150, 110, 180};
				def.inputs.EmplaceBack(MaterialNodePin{"A", MaterialDataType::Vec3, Vec4{1.0f, 1.0f, 1.0f, 1.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"B", MaterialDataType::Vec3, Vec4{1.0f, 1.0f, 1.0f, 1.0f}});
				def.outputs.EmplaceBack(MaterialNodePin{"Out", MaterialDataType::Vec3});
				def.codegen = CodegenMultiply;
				defs.EmplaceBack(Traits::Move(def));
			}
			{
				MaterialNodeDef def;
				def.typeId = "add";
				def.displayName = "Add";
				def.category = "Math";
				def.headerColor = {150, 110, 180};
				def.inputs.EmplaceBack(MaterialNodePin{"A", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"B", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f}});
				def.outputs.EmplaceBack(MaterialNodePin{"Out", MaterialDataType::Vec3});
				def.codegen = CodegenAdd;
				defs.EmplaceBack(Traits::Move(def));
			}
			{
				MaterialNodeDef def;
				def.typeId = "lerp";
				def.displayName = "Lerp";
				def.category = "Math";
				def.headerColor = {150, 110, 180};
				def.inputs.EmplaceBack(MaterialNodePin{"A", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"B", MaterialDataType::Vec3, Vec4{1.0f, 1.0f, 1.0f, 1.0f}});
				def.inputs.EmplaceBack(MaterialNodePin{"T", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f}});
				def.outputs.EmplaceBack(MaterialNodePin{"Out", MaterialDataType::Vec3});
				def.codegen = CodegenLerp;
				defs.EmplaceBack(Traits::Move(def));
			}

			return defs;
		}
	}

	Span<MaterialNodeDef> MaterialNodeRegistry::GetDefs()
	{
		static Array<MaterialNodeDef> defs = BuildDefs();
		return defs;
	}

	const MaterialNodeDef* MaterialNodeRegistry::Find(StringView typeId)
	{
		for (MaterialNodeDef& def : GetDefs())
		{
			if (def.typeId == typeId)
			{
				return &def;
			}
		}
		return nullptr;
	}

	StringView MaterialNodeRegistry::OutputTypeId()
	{
		return "output";
	}

	StringView MaterialHlslType(MaterialDataType type)
	{
		switch (type)
		{
			case MaterialDataType::Float: return "float";
			case MaterialDataType::Vec2:  return "float2";
			case MaterialDataType::Vec3:  return "float3";
			case MaterialDataType::Vec4:  return "float4";
		}
		return "float";
	}

	u32 MaterialComponentCount(MaterialDataType type)
	{
		switch (type)
		{
			case MaterialDataType::Float: return 1;
			case MaterialDataType::Vec2:  return 2;
			case MaterialDataType::Vec3:  return 3;
			case MaterialDataType::Vec4:  return 4;
		}
		return 1;
	}

	String MaterialConvertExpr(StringView expr, MaterialDataType from, MaterialDataType to)
	{
		String e{expr};
		if (from == to)
		{
			return e;
		}

		u32 fromCount = MaterialComponentCount(from);
		u32 toCount = MaterialComponentCount(to);

		//scalar -> vector: broadcast through a cast (HLSL replicates the scalar)
		if (fromCount == 1 && toCount > 1)
		{
			return String{"("} + MaterialHlslType(to) + ")(" + e + ")";
		}

		//anything -> scalar: take the first component
		if (toCount == 1)
		{
			return String{"("} + e + ").x";
		}

		//vector -> smaller vector: swizzle down
		if (toCount < fromCount)
		{
			StringView swizzle = toCount == 2 ? "xy" : "xyz";
			return String{"("} + e + ")." + swizzle;
		}

		//vector -> larger vector: pad the missing components with zeros
		String result = String{MaterialHlslType(to)} + "(" + e;
		for (u32 i = fromCount; i < toCount; ++i)
		{
			result += ", 0.0";
		}
		result += ")";
		return result;
	}

	String MaterialLiteralExpr(MaterialDataType type, Vec4 value)
	{
		switch (type)
		{
			case MaterialDataType::Float:
				return Flt(value.x);
			case MaterialDataType::Vec2:
				return "float2(" + Flt(value.x) + ", " + Flt(value.y) + ")";
			case MaterialDataType::Vec3:
				return "float3(" + Flt(value.x) + ", " + Flt(value.y) + ", " + Flt(value.z) + ")";
			case MaterialDataType::Vec4:
				return "float4(" + Flt(value.x) + ", " + Flt(value.y) + ", " + Flt(value.z) + ", " + Flt(value.w) + ")";
		}
		return Flt(value.x);
	}
}
