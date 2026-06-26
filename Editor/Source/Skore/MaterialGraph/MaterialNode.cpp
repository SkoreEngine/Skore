#include "MaterialNode.hpp"

#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Reflection.hpp"

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
	}

	//--- Output / master node --------------------------------------------------------------------
	struct MaterialOutputNode : MaterialNode
	{
		SK_CLASS(MaterialOutputNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "output"; }
		StringView        GetDisplayName() const override { return "Material Output"; }
		MaterialNodeColor GetHeaderColor() const override { return {200, 120, 80}; }
		bool              IsOutput() const override { return true; }

		void DefinePins() override
		{
			AddInput("Base Color", MaterialDataType::Vec3, Vec4{0.8f, 0.8f, 0.8f, 1.0f});
			AddInput("Metallic", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
			AddInput("Roughness", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f});
			AddInput("Emissive", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f});
		}

		//The compiler maps the output node's inputs directly to surface fields, so nothing to emit.
		void Generate(MaterialCodegenContext&) const override {}
	};

	//--- Constants -------------------------------------------------------------------------------
	struct MaterialConstantFloatNode : MaterialNode
	{
		SK_CLASS(MaterialConstantFloatNode, MaterialNode);

		StringView            GetNodeTypeId() const override { return "constant_float"; }
		StringView            GetDisplayName() const override { return "Float"; }
		StringView            GetCategory() const override { return "Constants"; }
		MaterialNodeColor     GetHeaderColor() const override { return {120, 170, 90}; }
		MaterialNodeValueKind GetValueKind() const override { return MaterialNodeValueKind::Float; }
		Vec4                  GetDefaultValue() const override { return Vec4{1.0f, 0.0f, 0.0f, 0.0f}; }

		void DefinePins() override { AddOutput("Out", MaterialDataType::Float); }

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, MaterialLiteralExpr(MaterialDataType::Float, ctx.value));
		}
	};

	struct MaterialConstantColorNode : MaterialNode
	{
		SK_CLASS(MaterialConstantColorNode, MaterialNode);

		StringView            GetNodeTypeId() const override { return "constant_color"; }
		StringView            GetDisplayName() const override { return "Color"; }
		StringView            GetCategory() const override { return "Constants"; }
		MaterialNodeColor     GetHeaderColor() const override { return {120, 170, 90}; }
		MaterialNodeValueKind GetValueKind() const override { return MaterialNodeValueKind::Color; }
		Vec4                  GetDefaultValue() const override { return Vec4{1.0f, 1.0f, 1.0f, 1.0f}; }

		void DefinePins() override { AddOutput("Out", MaterialDataType::Vec3); }

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, MaterialLiteralExpr(MaterialDataType::Vec3, ctx.value));
		}
	};

	struct MaterialConstantVec2Node : MaterialNode
	{
		SK_CLASS(MaterialConstantVec2Node, MaterialNode);

		StringView            GetNodeTypeId() const override { return "constant_vec2"; }
		StringView            GetDisplayName() const override { return "Vector2"; }
		StringView            GetCategory() const override { return "Constants"; }
		MaterialNodeColor     GetHeaderColor() const override { return {120, 170, 90}; }
		MaterialNodeValueKind GetValueKind() const override { return MaterialNodeValueKind::Vec2; }

		void DefinePins() override { AddOutput("Out", MaterialDataType::Vec2); }

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, MaterialLiteralExpr(MaterialDataType::Vec2, ctx.value));
		}
	};

	//--- Inputs ----------------------------------------------------------------------------------
	struct MaterialTexCoordNode : MaterialNode
	{
		SK_CLASS(MaterialTexCoordNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "tex_coord"; }
		StringView        GetDisplayName() const override { return "Texture Coordinate"; }
		StringView        GetCategory() const override { return "Inputs"; }
		MaterialNodeColor GetHeaderColor() const override { return {90, 150, 180}; }

		void DefinePins() override { AddOutput("UV", MaterialDataType::Vec2); }

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, "input.texCoord");
		}
	};

	//--- Math ------------------------------------------------------------------------------------
	struct MaterialMultiplyNode : MaterialNode
	{
		SK_CLASS(MaterialMultiplyNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "multiply"; }
		StringView        GetDisplayName() const override { return "Multiply"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Vec3, Vec4{1.0f, 1.0f, 1.0f, 1.0f});
			AddInput("B", MaterialDataType::Vec3, Vec4{1.0f, 1.0f, 1.0f, 1.0f});
			AddOutput("Out", MaterialDataType::Vec3);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " * " + ctx.Input(1) + ")");
		}
	};

	struct MaterialAddNode : MaterialNode
	{
		SK_CLASS(MaterialAddNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "add"; }
		StringView        GetDisplayName() const override { return "Add"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f});
			AddInput("B", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f});
			AddOutput("Out", MaterialDataType::Vec3);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " + " + ctx.Input(1) + ")");
		}
	};

	struct MaterialLerpNode : MaterialNode
	{
		SK_CLASS(MaterialLerpNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "lerp"; }
		StringView        GetDisplayName() const override { return "Lerp"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f});
			AddInput("B", MaterialDataType::Vec3, Vec4{1.0f, 1.0f, 1.0f, 1.0f});
			AddInput("T", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f});
			AddOutput("Out", MaterialDataType::Vec3);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"lerp("} + ctx.Input(0) + ", " + ctx.Input(1) + ", " + ctx.Input(2) + ")");
		}
	};

	//--- MaterialNode base -----------------------------------------------------------------------
	void MaterialNode::BuildPins()
	{
		m_inputs.Clear();
		m_outputs.Clear();
		DefinePins();
	}

	void MaterialNode::AddInput(StringView name, MaterialDataType type, Vec4 defaultValue)
	{
		m_inputs.EmplaceBack(MaterialNodePin{String{name}, type, defaultValue});
	}

	void MaterialNode::AddOutput(StringView name, MaterialDataType type)
	{
		m_outputs.EmplaceBack(MaterialNodePin{String{name}, type, Vec4{}});
	}

	//--- Registry --------------------------------------------------------------------------------
	void RegisterMaterialNodes()
	{
		Reflection::Type<MaterialNode>();
		Reflection::Type<MaterialOutputNode>();
		Reflection::Type<MaterialConstantFloatNode>();
		Reflection::Type<MaterialConstantColorNode>();
		Reflection::Type<MaterialConstantVec2Node>();
		Reflection::Type<MaterialTexCoordNode>();
		Reflection::Type<MaterialMultiplyNode>();
		Reflection::Type<MaterialAddNode>();
		Reflection::Type<MaterialLerpNode>();
	}

	Span<MaterialNode*> MaterialNodeRegistry::GetNodes()
	{
		static Array<MaterialNode*> nodes = []
		{
			Array<MaterialNode*> result;
			for (TypeID typeId : Reflection::GetDerivedTypes(TypeInfo<MaterialNode>::ID()))
			{
				ReflectType* type = Reflection::FindTypeById(typeId);
				if (!type)
				{
					continue;
				}

				Object* object = type->NewObject();
				if (!object)
				{
					continue;
				}

				if (MaterialNode* node = object->SafeCast<MaterialNode>())
				{
					node->BuildPins();
					result.EmplaceBack(node);
				}
				else
				{
					DestroyAndFree(object);
				}
			}
			return result;
		}();
		return nodes;
	}

	MaterialNode* MaterialNodeRegistry::Find(StringView typeId)
	{
		for (MaterialNode* node : GetNodes())
		{
			if (node->GetNodeTypeId() == typeId)
			{
				return node;
			}
		}
		return nullptr;
	}

	StringView MaterialNodeRegistry::OutputTypeId()
	{
		return "output";
	}

	//--- HLSL helpers ----------------------------------------------------------------------------
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
