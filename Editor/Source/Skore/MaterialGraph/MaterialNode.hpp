#pragma once

#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Object.hpp"
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

	//A node setting that isn't a pin (no incoming connection), edited in the Properties window.
	//Scalar/vector kinds store into MaterialGraphNodeResource::Value; Texture into ::Texture; Name into ::ParameterName.
	enum class MaterialNodePropertyType : u8
	{
		Texture, //a TextureResource reference, stored in MaterialGraphNodeResource::Texture
		Name,    //the exposed parameter name (String), stored in MaterialGraphNodeResource::ParameterName
		Float,   //Value.x
		Int,     //Value.x edited as an integer
		Bool,    //Value.x edited as a checkbox (0/1)
		Color,   //Value.xyz shown as a color swatch
		Vec2,    //Value.xy
		Vec3,    //Value.xyz
		Vec4,    //Value.xyzw
	};

	struct MaterialNodeProperty
	{
		String                   name{};
		MaterialNodePropertyType type = MaterialNodePropertyType::Texture;
	};

	struct MaterialNodePin
	{
		String           name{};
		MaterialDataType type = MaterialDataType::Float;
		Vec4             defaultValue{}; //literal used when the pin is left unconnected
		String           defaultExpr{}; //HLSL expr used when unconnected (overrides defaultValue), e.g. "mat.texCoord"
		bool             color = false;   //edit the unconnected literal as a color swatch instead of raw components
		bool             generic = false; //type adapts to the connected pins; `type` is the fallback (when nothing is connected)
	};

	struct MaterialNodeColor
	{
		u8 r = 60;
		u8 g = 110;
		u8 b = 180;
	};

	//Context passed to a node's Generate(). The compiler fills `inputs` (each already converted to the
	//matching input pin type), `value`, and `nodeIndex` (a unique per-node id). A node writes one HLSL
	//expression per output pin via SetOutput(), may emit helper statements that run before the output
	//variables via AddStatement(), and signals texture usage via UseTextures() + TextureSlot().
	struct MaterialCodegenContext
	{
		Span<String>   inputs{};
		Vec4           value{};
		u32            nodeIndex = 0;
		Array<String>& outputs;
		Array<String>& statements;
		bool*          usesTextures = nullptr;

		StringView Input(u32 index) const { return inputs[index]; }
		void       SetOutput(u32 index, StringView expr) { outputs[index] = expr; }
		void       AddStatement(StringView statement) { statements.EmplaceBack(String{statement}); }
		void       UseTextures() { if (usesTextures) *usesTextures = true; }
		u32        TextureSlot() const { return nodeIndex; }
	};

	//Abstract material graph node type. Subclass once per node; instances are singletons owned by
	//MaterialNodeRegistry and used for both editor UI info and HLSL code generation. To add a node:
	//  - derive from MaterialNode, add SK_CLASS(MyNode, MaterialNode)
	//  - override GetNodeTypeId()/GetDisplayName()/Generate() (others as needed)
	//  - declare pins in DefinePins() via AddInput()/AddOutput()
	//  - register it with Reflection::Type<MyNode>() inside RegisterMaterialNodes()
	struct MaterialNode : Object
	{
		SK_CLASS(MaterialNode, Object);

		~MaterialNode() override = default;

		virtual StringView            GetNodeTypeId() const = 0;
		virtual StringView            GetDisplayName() const = 0;
		virtual StringView            GetCategory() const { return ""; }
		virtual MaterialNodeColor     GetHeaderColor() const { return {}; }
		virtual bool                  IsOutput() const { return false; }

		//True for Parameters-category nodes: named, instance-overridable inputs. The exposed name lives on
		//the node resource (ParameterName); material instances override these by name.
		virtual bool                  IsParameter() const { return false; }
		virtual Vec4                  GetDefaultValue() const { return {}; }

		//Emit HLSL. Read ctx.Input(i)/ctx.value, write ctx.SetOutput(i, expr).
		virtual void Generate(MaterialCodegenContext& ctx) const = 0;

		Span<MaterialNodePin>      GetInputs() const { return m_inputs; }
		Span<MaterialNodePin>      GetOutputs() const { return m_outputs; }
		Span<MaterialNodeProperty> GetProperties() const { return m_properties; }

		//Rebuilds the pin and property lists by invoking DefinePins()/DefineProperties(). Called by the
		//registry after construction.
		void BuildPins();

	protected:
		virtual void DefinePins() {}
		virtual void DefineProperties() {}
		void         AddInput(StringView name, MaterialDataType type, Vec4 defaultValue = {}, StringView defaultExpr = {}, bool color = false, bool generic = false);
		void         AddOutput(StringView name, MaterialDataType type, bool generic = false);
		void         AddProperty(StringView name, MaterialNodePropertyType type);

	private:
		Array<MaterialNodePin>      m_inputs{};
		Array<MaterialNodePin>      m_outputs{};
		Array<MaterialNodeProperty> m_properties{};
	};

	//Global registry of available material node types, populated from reflection on first access.
	struct SK_API MaterialNodeRegistry
	{
		static Span<MaterialNode*> GetNodes();
		static MaterialNode*       Find(StringView typeId);

		//Fixed input pin layout of the output (master) node, mapped to surface fields by the compiler.
		enum OutputPin
		{
			BaseColor   = 0,
			Metallic    = 1,
			Roughness   = 2,
			Emissive    = 3,
			Normal      = 4,
			Occlusion   = 5,
			Opacity     = 6,
			OpacityMask = 7,
		};

		static StringView OutputTypeId();
	};

	//Registers all built-in node types with reflection. Call before the registry is first used.
	SK_API void RegisterMaterialNodes();

	SK_API StringView MaterialHlslType(MaterialDataType type);
	SK_API u32        MaterialComponentCount(MaterialDataType type);

	//Builds an HLSL expression converting `expr` (of `from` type) to the `to` type.
	SK_API String MaterialConvertExpr(StringView expr, MaterialDataType from, MaterialDataType to);

	//Literal HLSL expression for a value of `type` taken from `value`.
	SK_API String MaterialLiteralExpr(MaterialDataType type, Vec4 value);

	//Reads the per-instance literal override stored for an input pin of `node`, returning `fallback`
	//(the pin's default) when the node has no override for that pin.
	SK_API Vec4 MaterialReadPinValue(RID node, u32 pinIndex, Vec4 fallback);
}
