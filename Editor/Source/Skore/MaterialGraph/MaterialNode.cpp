#include "MaterialNode.hpp"

#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"

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

		String IndexStr(u32 v)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%u", v);
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
			AddInput("Base Color", MaterialDataType::Vec3, Vec4{0.8f, 0.8f, 0.8f, 1.0f}, {}, true);
			AddInput("Metallic", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
			AddInput("Roughness", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f});
			AddInput("Emissive", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, true);
			AddInput("Normal", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 1.0f, 0.0f}); //tangent-space
			AddInput("Ambient Occlusion", MaterialDataType::Float, Vec4{1.0f, 0.0f, 0.0f, 0.0f});
			AddInput("Opacity", MaterialDataType::Float, Vec4{1.0f, 0.0f, 0.0f, 0.0f});
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

	//--- Texture ---------------------------------------------------------------------------------
	struct MaterialSampleTexture2DNode : MaterialNode
	{
		SK_CLASS(MaterialSampleTexture2DNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "sample_texture"; }
		StringView        GetDisplayName() const override { return "Sample Texture 2D"; }
		StringView        GetCategory() const override { return "Texture"; }
		MaterialNodeColor GetHeaderColor() const override { return {180, 130, 70}; }

		void DefinePins() override
		{
			AddInput("UV", MaterialDataType::Vec2, Vec4{}, "input.texCoord");
			AddOutput("RGBA", MaterialDataType::Vec4);
			AddOutput("R", MaterialDataType::Float);
			AddOutput("G", MaterialDataType::Float);
			AddOutput("B", MaterialDataType::Float);
			AddOutput("A", MaterialDataType::Float);
		}

		void DefineProperties() override
		{
			AddProperty("Texture", MaterialNodePropertyType::Texture);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.UseTextures();
			String slot = IndexStr(ctx.TextureSlot());
			String sample = String{"tex"} + IndexStr(ctx.nodeIndex);
			ctx.AddStatement(String{"float4 "} + sample + " = MaterialTextures[" + slot + "].Sample(MaterialSampler, " + ctx.Input(0) + ");");
			ctx.SetOutput(0, sample);
			ctx.SetOutput(1, sample + ".r");
			ctx.SetOutput(2, sample + ".g");
			ctx.SetOutput(3, sample + ".b");
			ctx.SetOutput(4, sample + ".a");
		}
	};

	struct MaterialNormalMapNode : MaterialNode
	{
		SK_CLASS(MaterialNormalMapNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "normal_map"; }
		StringView        GetDisplayName() const override { return "Normal Map"; }
		StringView        GetCategory() const override { return "Texture"; }
		MaterialNodeColor GetHeaderColor() const override { return {180, 130, 70}; }

		void DefinePins() override
		{
			AddInput("UV", MaterialDataType::Vec2, Vec4{}, "input.texCoord");
			AddInput("Strength", MaterialDataType::Float, Vec4{1.0f, 0.0f, 0.0f, 0.0f});
			AddOutput("Normal", MaterialDataType::Vec3);
		}

		void DefineProperties() override
		{
			AddProperty("Texture", MaterialNodePropertyType::Texture);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.UseTextures();
			String slot = IndexStr(ctx.TextureSlot());
			String temp = String{"nm"} + IndexStr(ctx.nodeIndex);
			ctx.AddStatement(String{"float3 "} + temp + " = MaterialTextures[" + slot + "].Sample(MaterialSampler, " + ctx.Input(0) + ").xyz * 2.0 - 1.0;");
			ctx.SetOutput(0, String{"normalize(float3("} + temp + ".xy * " + ctx.Input(1) + ", " + temp + ".z))");
		}
	};

	struct MaterialTilingOffsetNode : MaterialNode
	{
		SK_CLASS(MaterialTilingOffsetNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "tiling_offset"; }
		StringView        GetDisplayName() const override { return "Tiling & Offset"; }
		StringView        GetCategory() const override { return "Texture"; }
		MaterialNodeColor GetHeaderColor() const override { return {180, 130, 70}; }

		void DefinePins() override
		{
			AddInput("UV", MaterialDataType::Vec2, Vec4{}, "input.texCoord");
			AddInput("Tiling", MaterialDataType::Vec2, Vec4{1.0f, 1.0f, 0.0f, 0.0f});
			AddInput("Offset", MaterialDataType::Vec2, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
			AddOutput("UV", MaterialDataType::Vec2);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " * " + ctx.Input(1) + " + " + ctx.Input(2) + ")");
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
			AddInput("A", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
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
			AddInput("A", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
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
			AddInput("A", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddInput("T", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f});
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"lerp("} + ctx.Input(0) + ", " + ctx.Input(1) + ", " + ctx.Input(2) + ")");
		}
	};

	struct MaterialSubtractNode : MaterialNode
	{
		SK_CLASS(MaterialSubtractNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "subtract"; }
		StringView        GetDisplayName() const override { return "Subtract"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " - " + ctx.Input(1) + ")");
		}
	};

	struct MaterialDivideNode : MaterialNode
	{
		SK_CLASS(MaterialDivideNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "divide"; }
		StringView        GetDisplayName() const override { return "Divide"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"("} + ctx.Input(0) + " / " + ctx.Input(1) + ")");
		}
	};

	struct MaterialPowerNode : MaterialNode
	{
		SK_CLASS(MaterialPowerNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "power"; }
		StringView        GetDisplayName() const override { return "Power"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("Base", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddInput("Exp", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"pow("} + ctx.Input(0) + ", " + ctx.Input(1) + ")");
		}
	};

	struct MaterialMinNode : MaterialNode
	{
		SK_CLASS(MaterialMinNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "min"; }
		StringView        GetDisplayName() const override { return "Min"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"min("} + ctx.Input(0) + ", " + ctx.Input(1) + ")");
		}
	};

	struct MaterialMaxNode : MaterialNode
	{
		SK_CLASS(MaterialMaxNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "max"; }
		StringView        GetDisplayName() const override { return "Max"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"max("} + ctx.Input(0) + ", " + ctx.Input(1) + ")");
		}
	};

	struct MaterialStepNode : MaterialNode
	{
		SK_CLASS(MaterialStepNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "step"; }
		StringView        GetDisplayName() const override { return "Step"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("Edge", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f}, {}, false, true);
			AddInput("X", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"step("} + ctx.Input(0) + ", " + ctx.Input(1) + ")");
		}
	};

	struct MaterialOneMinusNode : MaterialNode
	{
		SK_CLASS(MaterialOneMinusNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "one_minus"; }
		StringView        GetDisplayName() const override { return "One Minus"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("In", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"(1.0 - "} + ctx.Input(0) + ")");
		}
	};

	struct MaterialSaturateNode : MaterialNode
	{
		SK_CLASS(MaterialSaturateNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "saturate"; }
		StringView        GetDisplayName() const override { return "Saturate"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("In", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"saturate("} + ctx.Input(0) + ")");
		}
	};

	struct MaterialClampNode : MaterialNode
	{
		SK_CLASS(MaterialClampNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "clamp"; }
		StringView        GetDisplayName() const override { return "Clamp"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("In", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("Min", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f}, {}, false, true);
			AddInput("Max", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"clamp("} + ctx.Input(0) + ", " + ctx.Input(1) + ", " + ctx.Input(2) + ")");
		}
	};

	struct MaterialSmoothstepNode : MaterialNode
	{
		SK_CLASS(MaterialSmoothstepNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "smoothstep"; }
		StringView        GetDisplayName() const override { return "Smoothstep"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("Min", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f}, {}, false, true);
			AddInput("Max", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddInput("In", MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"smoothstep("} + ctx.Input(0) + ", " + ctx.Input(1) + ", " + ctx.Input(2) + ")");
		}
	};

	struct MaterialRemapNode : MaterialNode
	{
		SK_CLASS(MaterialRemapNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "remap"; }
		StringView        GetDisplayName() const override { return "Remap"; }
		StringView        GetCategory() const override { return "Math"; }
		MaterialNodeColor GetHeaderColor() const override { return {150, 110, 180}; }

		void DefinePins() override
		{
			AddInput("In", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {}, false, true);
			AddInput("In Min", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f}, {}, false, true);
			AddInput("In Max", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddInput("Out Min", MaterialDataType::Float, Vec4{0.0f, 0.0f, 0.0f, 0.0f}, {}, false, true);
			AddInput("Out Max", MaterialDataType::Float, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"(("} + ctx.Input(0) + " - " + ctx.Input(1) + ") / (" + ctx.Input(2) + " - " + ctx.Input(1) + ") * (" + ctx.Input(4) + " - " + ctx.Input(3) + ") + " + ctx.Input(3) + ")");
		}
	};

	//--- Vector ----------------------------------------------------------------------------------
	struct MaterialDotNode : MaterialNode
	{
		SK_CLASS(MaterialDotNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "dot"; }
		StringView        GetDisplayName() const override { return "Dot"; }
		StringView        GetCategory() const override { return "Vector"; }
		MaterialNodeColor GetHeaderColor() const override { return {100, 140, 200}; }

		void DefinePins() override
		{
			AddInput("A", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 1.0f, 0.0f}, {}, false, true);
			AddInput("B", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 1.0f, 0.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"dot("} + ctx.Input(0) + ", " + ctx.Input(1) + ")");
		}
	};

	struct MaterialNormalizeNode : MaterialNode
	{
		SK_CLASS(MaterialNormalizeNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "normalize"; }
		StringView        GetDisplayName() const override { return "Normalize"; }
		StringView        GetCategory() const override { return "Vector"; }
		MaterialNodeColor GetHeaderColor() const override { return {100, 140, 200}; }

		void DefinePins() override
		{
			AddInput("In", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 1.0f, 0.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Vec3, true);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"normalize("} + ctx.Input(0) + ")");
		}
	};

	struct MaterialLengthNode : MaterialNode
	{
		SK_CLASS(MaterialLengthNode, MaterialNode);

		StringView        GetNodeTypeId() const override { return "length"; }
		StringView        GetDisplayName() const override { return "Length"; }
		StringView        GetCategory() const override { return "Vector"; }
		MaterialNodeColor GetHeaderColor() const override { return {100, 140, 200}; }

		void DefinePins() override
		{
			AddInput("In", MaterialDataType::Vec3, Vec4{0.0f, 0.0f, 1.0f, 0.0f}, {}, false, true);
			AddOutput("Out", MaterialDataType::Float);
		}

		void Generate(MaterialCodegenContext& ctx) const override
		{
			ctx.SetOutput(0, String{"length("} + ctx.Input(0) + ")");
		}
	};

	//--- MaterialNode base -----------------------------------------------------------------------
	void MaterialNode::BuildPins()
	{
		m_inputs.Clear();
		m_outputs.Clear();
		m_properties.Clear();
		DefinePins();
		DefineProperties();
	}

	void MaterialNode::AddInput(StringView name, MaterialDataType type, Vec4 defaultValue, StringView defaultExpr, bool color, bool generic)
	{
		m_inputs.EmplaceBack(MaterialNodePin{String{name}, type, defaultValue, String{defaultExpr}, color, generic});
	}

	void MaterialNode::AddOutput(StringView name, MaterialDataType type, bool generic)
	{
		m_outputs.EmplaceBack(MaterialNodePin{String{name}, type, Vec4{}, String{}, false, generic});
	}

	void MaterialNode::AddProperty(StringView name, MaterialNodePropertyType type)
	{
		m_properties.EmplaceBack(MaterialNodeProperty{String{name}, type});
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
		Reflection::Type<MaterialSampleTexture2DNode>();
		Reflection::Type<MaterialNormalMapNode>();
		Reflection::Type<MaterialTilingOffsetNode>();
		Reflection::Type<MaterialMultiplyNode>();
		Reflection::Type<MaterialAddNode>();
		Reflection::Type<MaterialLerpNode>();
		Reflection::Type<MaterialSubtractNode>();
		Reflection::Type<MaterialDivideNode>();
		Reflection::Type<MaterialPowerNode>();
		Reflection::Type<MaterialMinNode>();
		Reflection::Type<MaterialMaxNode>();
		Reflection::Type<MaterialStepNode>();
		Reflection::Type<MaterialOneMinusNode>();
		Reflection::Type<MaterialSaturateNode>();
		Reflection::Type<MaterialClampNode>();
		Reflection::Type<MaterialSmoothstepNode>();
		Reflection::Type<MaterialRemapNode>();
		Reflection::Type<MaterialDotNode>();
		Reflection::Type<MaterialNormalizeNode>();
		Reflection::Type<MaterialLengthNode>();
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

	Vec4 MaterialReadPinValue(RID node, u32 pinIndex, Vec4 fallback)
	{
		ResourceObject nodeObj = Resources::Read(node);
		if (!nodeObj)
		{
			return fallback;
		}

		for (RID entry : nodeObj.GetSubObjectList(MaterialGraphNodeResource::InputValues))
		{
			ResourceObject entryObj = Resources::Read(entry);
			if (entryObj && static_cast<u32>(entryObj.GetUInt(MaterialGraphPinValueResource::PinIndex)) == pinIndex)
			{
				return entryObj.GetVec4(MaterialGraphPinValueResource::Value);
			}
		}

		return fallback;
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
