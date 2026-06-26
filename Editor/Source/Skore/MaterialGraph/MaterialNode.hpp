#pragma once

#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	//Scalar/vector type flowing through the material graph.
	enum class MaterialDataType : u8
	{
		Float,
		Vec2,
		Vec3,
		Vec4,
	};

	//How a node's literal Value field is edited in the inspector.
	enum class MaterialNodeValueKind : u8
	{
		None,  //no editable literal
		Float, //value.x
		Vec2,  //value.xy
		Color, //value.xyz shown as a color swatch
	};

	struct MaterialNodePin
	{
		String           name{};
		MaterialDataType type = MaterialDataType::Float;
		Vec4             defaultValue{}; //used when the pin is left unconnected
	};

	//Context passed to a node's codegen function. The compiler fills `inputs` (each already
	//converted to the matching input pin type) and `value`, then the codegen writes one HLSL
	//expression per output pin into `outputs`.
	struct MaterialCodegenContext
	{
		Span<String>   inputs{};
		Vec4           value{};
		Array<String>& outputs;

		StringView Input(u32 index) const { return inputs[index]; }
		void       SetOutput(u32 index, StringView expr) { outputs[index] = expr; }
	};

	using FnMaterialNodeCodegen = void (*)(MaterialCodegenContext& ctx);

	struct MaterialNodeColor
	{
		u8 r = 60;
		u8 g = 110;
		u8 b = 180;
	};

	struct MaterialNodeDef
	{
		String                 typeId{};
		String                 displayName{};
		String                 category{};
		Array<MaterialNodePin> inputs{};
		Array<MaterialNodePin> outputs{};
		MaterialNodeValueKind  valueKind = MaterialNodeValueKind::None;
		Vec4                   defaultValue{};
		MaterialNodeColor      headerColor{};
		bool                   isOutput = false; //the single master node of a graph
		FnMaterialNodeCodegen  codegen = nullptr;
	};

	//Global registry of available material node types. Built lazily on first access.
	struct MaterialNodeRegistry
	{
		static Span<MaterialNodeDef> GetDefs();
		static const MaterialNodeDef* Find(StringView typeId);

		//Fixed input pin layout of the output (master) node, mapped to surface fields by the compiler.
		enum OutputPin
		{
			BaseColor = 0,
			Metallic  = 1,
			Roughness = 2,
			Emissive  = 3,
		};

		static StringView OutputTypeId();
	};

	StringView MaterialHlslType(MaterialDataType type);
	u32        MaterialComponentCount(MaterialDataType type);

	//Builds an HLSL expression converting `expr` (of `from` type) to the `to` type.
	String MaterialConvertExpr(StringView expr, MaterialDataType from, MaterialDataType to);

	//Literal HLSL expression for a value of `type` taken from `value`.
	String MaterialLiteralExpr(MaterialDataType type, Vec4 value);
}
