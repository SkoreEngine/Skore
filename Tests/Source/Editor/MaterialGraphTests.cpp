#include "doctest.h"

#include "Skore/Core/Math.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/MaterialGraph/MaterialGraphCompiler.hpp"
#include "Skore/MaterialGraph/MaterialNode.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Utils/ShaderManager.hpp"

#include <string_view>

using namespace Skore;

namespace Skore
{
	void SK_API ResourceInit();
	void SK_API ResourceShutdown();
	void SK_API ShaderManagerInit();
	void SK_API ShaderManagerShutdown();
}

namespace
{
	bool Contains(const String& haystack, const char* needle)
	{
		return std::string_view(haystack.CStr(), haystack.Size()).find(needle) != std::string_view::npos;
	}

	void SetupMaterialGraphTypes()
	{
		//Texture field is omitted on purpose: it references TextureResource which these tests
		//don't register, and none of the tested paths touch it.
		Resources::Type<MaterialGraphNodeResource>()
			.Field<MaterialGraphNodeResource::Type>(ResourceFieldType::String)
			.Field<MaterialGraphNodeResource::Position>(ResourceFieldType::Vec2)
			.Field<MaterialGraphNodeResource::Value>(ResourceFieldType::Vec4)
			.Build();

		Resources::Type<MaterialGraphConnectionResource>()
			.Field<MaterialGraphConnectionResource::OutputNode>(ResourceFieldType::Reference, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphConnectionResource::OutputPin>(ResourceFieldType::UInt)
			.Field<MaterialGraphConnectionResource::InputNode>(ResourceFieldType::Reference, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphConnectionResource::InputPin>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<MaterialGraphResource>()
			.Field<MaterialGraphResource::Name>(ResourceFieldType::String)
			.Field<MaterialGraphResource::Nodes>(ResourceFieldType::SubObjectList, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphResource::Connections>(ResourceFieldType::SubObjectList, TypeInfo<MaterialGraphConnectionResource>::ID())
			.Field<MaterialGraphResource::OutputNode>(ResourceFieldType::Reference, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphResource::AlphaMode>(ResourceFieldType::Enum, TypeInfo<MaterialGraphResource::GraphAlphaMode>::ID())
			.Field<MaterialGraphResource::MaskCutoff>(ResourceFieldType::Float)
			.Build();
	}

	RID AddNode(RID graph, StringView typeId, Vec4 value)
	{
		RID node = Resources::Create<MaterialGraphNodeResource>(UUID::RandomUUID());
		{
			ResourceObject nodeObj = Resources::Write(node);
			nodeObj.SetString(MaterialGraphNodeResource::Type, typeId);
			nodeObj.SetVec4(MaterialGraphNodeResource::Value, value);
			nodeObj.Commit();
		}
		ResourceObject graphObj = Resources::Write(graph);
		graphObj.AddToSubObjectList(MaterialGraphResource::Nodes, node);
		graphObj.Commit();
		return node;
	}

	RID Connect(RID graph, RID outputNode, u32 outputPin, RID inputNode, u32 inputPin)
	{
		RID conn = Resources::Create<MaterialGraphConnectionResource>(UUID::RandomUUID());
		{
			ResourceObject connObj = Resources::Write(conn);
			connObj.SetReference(MaterialGraphConnectionResource::OutputNode, outputNode);
			connObj.SetUInt(MaterialGraphConnectionResource::OutputPin, outputPin);
			connObj.SetReference(MaterialGraphConnectionResource::InputNode, inputNode);
			connObj.SetUInt(MaterialGraphConnectionResource::InputPin, inputPin);
			connObj.Commit();
		}
		ResourceObject graphObj = Resources::Write(graph);
		graphObj.AddToSubObjectList(MaterialGraphResource::Connections, conn);
		graphObj.Commit();
		return conn;
	}

	RID NewGraph(RID& outputNode)
	{
		RID graph = Resources::Create<MaterialGraphResource>(UUID::RandomUUID());
		outputNode = AddNode(graph, MaterialNodeRegistry::OutputTypeId(), Vec4{});
		ResourceObject graphObj = Resources::Write(graph);
		graphObj.SetReference(MaterialGraphResource::OutputNode, outputNode);
		graphObj.Commit();
		return graph;
	}

	// --- Pure HLSL helpers -------------------------------------------------------------------

	TEST_CASE("MaterialGraph::HlslType")
	{
		CHECK(MaterialHlslType(MaterialDataType::Float) == "float");
		CHECK(MaterialHlslType(MaterialDataType::Vec2) == "float2");
		CHECK(MaterialHlslType(MaterialDataType::Vec3) == "float3");
		CHECK(MaterialHlslType(MaterialDataType::Vec4) == "float4");

		CHECK(MaterialComponentCount(MaterialDataType::Float) == 1);
		CHECK(MaterialComponentCount(MaterialDataType::Vec2) == 2);
		CHECK(MaterialComponentCount(MaterialDataType::Vec3) == 3);
		CHECK(MaterialComponentCount(MaterialDataType::Vec4) == 4);
	}

	TEST_CASE("MaterialGraph::ConvertExpr")
	{
		// identity
		CHECK(MaterialConvertExpr("v", MaterialDataType::Vec3, MaterialDataType::Vec3) == "v");
		// scalar -> vector broadcast
		CHECK(MaterialConvertExpr("x", MaterialDataType::Float, MaterialDataType::Vec3) == "(float3)(x)");
		// vector -> scalar takes .x
		CHECK(MaterialConvertExpr("v", MaterialDataType::Vec3, MaterialDataType::Float) == "(v).x");
		// vector -> smaller vector swizzles
		CHECK(MaterialConvertExpr("v", MaterialDataType::Vec3, MaterialDataType::Vec2) == "(v).xy");
		// vector -> larger vector pads with zeros
		CHECK(MaterialConvertExpr("v", MaterialDataType::Vec2, MaterialDataType::Vec3) == "float3(v, 0.0)");
	}

	TEST_CASE("MaterialGraph::LiteralExpr")
	{
		CHECK(MaterialLiteralExpr(MaterialDataType::Float, Vec4{0.5f, 0.0f, 0.0f, 0.0f}) == "0.5");
		CHECK(MaterialLiteralExpr(MaterialDataType::Vec3, Vec4{1.0f, 0.0f, 0.0f, 1.0f}) == "float3(1, 0, 0)");
		CHECK(MaterialLiteralExpr(MaterialDataType::Vec2, Vec4{2.0f, 3.0f, 0.0f, 0.0f}) == "float2(2, 3)");
	}

	// --- Node registry -----------------------------------------------------------------------

	TEST_CASE("MaterialGraph::NodeRegistry")
	{
		ResourceInit();
		RegisterMaterialNodes();

		Span<MaterialNode*> nodes = MaterialNodeRegistry::GetNodes();
		CHECK(nodes.Size() >= 11);

		MaterialNode* output = MaterialNodeRegistry::Find("output");
		REQUIRE(output != nullptr);
		CHECK(output->IsOutput());
		CHECK(output->GetInputs().Size() == 8);
		CHECK(output->GetOutputs().Size() == 0);
		CHECK(output->GetInputs()[MaterialNodeRegistry::BaseColor].type == MaterialDataType::Vec3);
		CHECK(output->GetInputs()[MaterialNodeRegistry::Metallic].type == MaterialDataType::Float);
		CHECK(output->GetInputs()[MaterialNodeRegistry::Normal].type == MaterialDataType::Vec3);
		CHECK(output->GetInputs()[MaterialNodeRegistry::Opacity].type == MaterialDataType::Float);

		MaterialNode* sampleTexture = MaterialNodeRegistry::Find("sample_texture");
		REQUIRE(sampleTexture != nullptr);
		CHECK(sampleTexture->GetInputs().Size() == 1);
		// RGBA plus the four separated channels
		REQUIRE(sampleTexture->GetOutputs().Size() == 5);
		CHECK(sampleTexture->GetOutputs()[0].type == MaterialDataType::Vec4);
		CHECK(sampleTexture->GetOutputs()[0].name == "RGBA");
		CHECK(sampleTexture->GetOutputs()[1].type == MaterialDataType::Float);
		CHECK(sampleTexture->GetOutputs()[1].name == "R");
		CHECK(sampleTexture->GetOutputs()[2].name == "G");
		CHECK(sampleTexture->GetOutputs()[3].name == "B");
		CHECK(sampleTexture->GetOutputs()[4].name == "A");

		CHECK(MaterialNodeRegistry::Find("normal_map") != nullptr);
		CHECK(MaterialNodeRegistry::Find("tiling_offset") != nullptr);

		MaterialNode* multiply = MaterialNodeRegistry::Find("multiply");
		REQUIRE(multiply != nullptr);
		CHECK_FALSE(multiply->IsOutput());
		CHECK(multiply->GetInputs().Size() == 2);
		CHECK(multiply->GetOutputs().Size() == 1);
		// math nodes are generic: pins fall back to Float and resolve their real type from connections
		CHECK(multiply->GetInputs()[0].generic);
		CHECK(multiply->GetInputs()[1].generic);
		CHECK(multiply->GetOutputs()[0].generic);
		CHECK(multiply->GetOutputs()[0].type == MaterialDataType::Float);

		MaterialNode* constantFloat = MaterialNodeRegistry::Find("constant_float");
		REQUIRE(constantFloat != nullptr);
		REQUIRE(constantFloat->GetProperties().Size() == 1);
		CHECK(constantFloat->GetProperties()[0].type == MaterialNodePropertyType::Float);
		CHECK(constantFloat->GetOutputs()[0].type == MaterialDataType::Float);

		MaterialNode* color = MaterialNodeRegistry::Find("constant_color");
		REQUIRE(color != nullptr);
		REQUIRE(color->GetProperties().Size() == 1);
		CHECK(color->GetProperties()[0].type == MaterialNodePropertyType::Color);

		MaterialNode* texCoord = MaterialNodeRegistry::Find("tex_coord");
		REQUIRE(texCoord != nullptr);
		CHECK(texCoord->GetOutputs()[0].type == MaterialDataType::Vec2);

		// Parameters: every parameter node exposes a Name plus a typed default, both edited in Properties.
		const char* paramTypes[] = {"param_float", "param_int", "param_bool", "param_color", "param_vec2", "param_vec3", "param_vec4", "param_texture2d"};
		for (const char* typeId : paramTypes)
		{
			MaterialNode* param = MaterialNodeRegistry::Find(typeId);
			REQUIRE(param != nullptr);
			CHECK(param->GetCategory() == "Parameters");
			REQUIRE(param->GetProperties().Size() == 2);
			CHECK(param->GetProperties()[0].type == MaterialNodePropertyType::Name);
		}

		MaterialNode* paramColor = MaterialNodeRegistry::Find("param_color");
		CHECK(paramColor->GetProperties()[1].type == MaterialNodePropertyType::Color);
		CHECK(paramColor->GetOutputs()[0].type == MaterialDataType::Vec3);

		// the Texture2D parameter samples like sample_texture: its default is a texture reference
		MaterialNode* paramTexture = MaterialNodeRegistry::Find("param_texture2d");
		CHECK(paramTexture->GetProperties()[1].type == MaterialNodePropertyType::Texture);
		REQUIRE(paramTexture->GetOutputs().Size() == 5);
		CHECK(paramTexture->GetOutputs()[0].type == MaterialDataType::Vec4);

		CHECK(MaterialNodeRegistry::Find("does_not_exist") == nullptr);

		ResourceShutdown();
	}

	// --- Resource data model -----------------------------------------------------------------

	TEST_CASE("MaterialGraph::ResourceModel")
	{
		ResourceInit();
		SetupMaterialGraphTypes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		ResourceObject graphObj = Resources::Write(graph);
		graphObj.SetString(MaterialGraphResource::Name, "TestGraph");
		graphObj.Commit();

		RID colorNode = AddNode(graph, "constant_color", Vec4{1.0f, 0.0f, 0.0f, 1.0f});
		Connect(graph, colorNode, 0, outputNode, MaterialNodeRegistry::BaseColor);

		ResourceObject readGraph = Resources::Read(graph);
		CHECK(readGraph.GetString(MaterialGraphResource::Name) == "TestGraph");
		CHECK(readGraph.GetSubObjectList(MaterialGraphResource::Nodes).Size() == 2);
		CHECK(readGraph.GetSubObjectList(MaterialGraphResource::Connections).Size() == 1);
		CHECK(readGraph.GetReference(MaterialGraphResource::OutputNode) == outputNode);

		Span<RID> conns = readGraph.GetSubObjectList(MaterialGraphResource::Connections);
		REQUIRE(conns.Size() == 1);
		ResourceObject connObj = Resources::Read(conns[0]);
		CHECK(connObj.GetReference(MaterialGraphConnectionResource::OutputNode) == colorNode);
		CHECK(connObj.GetReference(MaterialGraphConnectionResource::InputNode) == outputNode);
		CHECK(connObj.GetUInt(MaterialGraphConnectionResource::InputPin) == MaterialNodeRegistry::BaseColor);

		ResourceObject colorObj = Resources::Read(colorNode);
		CHECK(colorObj.GetString(MaterialGraphNodeResource::Type) == "constant_color");
		CHECK(colorObj.GetVec4(MaterialGraphNodeResource::Value).x == 1.0f);

		ResourceShutdown();
	}

	// --- HLSL generation ---------------------------------------------------------------------

	TEST_CASE("MaterialGraph::GenerateBodyDefaults")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		CHECK(!body.Empty());
		// the body assigns the master node's fields onto the template's `surface` struct
		CHECK(Contains(body, "surface.baseColor = float3(0.8, 0.8, 0.8)"));
		CHECK(Contains(body, "surface.roughness = 0.5"));
		CHECK(Contains(body, "surface.normal"));
		CHECK(Contains(body, "surface.occlusion"));
		CHECK(Contains(body, "surface.opacity"));
		CHECK(Contains(body, "float3(0, 0, 1)")); // tangent-space normal default
		// default Opaque mode forces a solid surface and never discards
		CHECK(Contains(body, "surface.opacity   = 1.0;"));
		CHECK(!Contains(body, "discard"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::AlphaModeMaskDiscards")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);
		{
			ResourceObject graphObj = Resources::Write(graph);
			graphObj.SetEnum(MaterialGraphResource::AlphaMode, MaterialGraphResource::GraphAlphaMode::Mask);
			graphObj.SetFloat(MaterialGraphResource::MaskCutoff, 0.25f);
			graphObj.Commit();
		}

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		// Mask mode clips against the cutoff and keeps the surface opaque
		CHECK(Contains(body, "discard"));
		CHECK(Contains(body, "0.25"));
		CHECK(Contains(body, "surface.opacity   = 1.0;"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::AlphaModeBlendOutputsOpacity")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);
		{
			ResourceObject graphObj = Resources::Write(graph);
			graphObj.SetEnum(MaterialGraphResource::AlphaMode, MaterialGraphResource::GraphAlphaMode::Blend);
			graphObj.Commit();
		}

		RID opacityNode = AddNode(graph, "constant_float", Vec4{0.3f, 0.0f, 0.0f, 0.0f});
		Connect(graph, opacityNode, 0, outputNode, MaterialNodeRegistry::Opacity);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		// Blend mode reads the Opacity pin into the surface alpha and never discards
		CHECK(Contains(body, "surface.opacity   = "));
		CHECK(Contains(body, "0.3"));
		CHECK(!Contains(body, "discard"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateBodyTexture")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		RID texNode = AddNode(graph, "sample_texture", Vec4{});
		Connect(graph, texNode, 0, outputNode, MaterialNodeRegistry::BaseColor);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		CHECK(Contains(body, ".Sample(MaterialSampler,"));
		// unconnected UV falls back to the mesh UVs via the pin's default expression
		CHECK(Contains(body, "mat.texCoord"));
		// the Vec4 sample feeding a Vec3 input is swizzled down
		CHECK(Contains(body, ".xyz"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateBodyTextureChannel")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		RID texNode = AddNode(graph, "sample_texture", Vec4{});
		// Output pin 1 is the separated R channel (Float) feeding the Float Roughness input.
		Connect(graph, texNode, 1, outputNode, MaterialNodeRegistry::Roughness);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		// The texture is sampled once into a temp and the channel is taken via swizzle.
		CHECK(Contains(body, ".Sample(MaterialSampler,"));
		CHECK(Contains(body, ".r"));
		CHECK(Contains(body, "surface.roughness ="));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateBodyConnected")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		RID colorNode = AddNode(graph, "constant_color", Vec4{1.0f, 0.0f, 0.0f, 1.0f});
		RID roughNode = AddNode(graph, "constant_float", Vec4{0.25f, 0.0f, 0.0f, 0.0f});

		Connect(graph, colorNode, 0, outputNode, MaterialNodeRegistry::BaseColor);
		Connect(graph, roughNode, 0, outputNode, MaterialNodeRegistry::Roughness);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		CHECK(Contains(body, "float3 n"));            // a temp var was emitted for an upstream node
		CHECK(Contains(body, "float3(1, 0, 0)"));     // the color literal
		CHECK(Contains(body, "0.25"));                // the roughness literal
		CHECK(Contains(body, "surface.baseColor ="));
		CHECK(Contains(body, "surface.roughness ="));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateBodyTypeConversion")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		// Float output feeding a Vec3 input must be broadcast.
		RID floatNode = AddNode(graph, "constant_float", Vec4{0.7f, 0.0f, 0.0f, 0.0f});
		Connect(graph, floatNode, 0, outputNode, MaterialNodeRegistry::BaseColor);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		CHECK(Contains(body, "(float3)("));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenericMathFloat")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		// float * float resolves the Multiply node to Float (no vector promotion).
		RID a = AddNode(graph, "constant_float", Vec4{2.0f, 0.0f, 0.0f, 0.0f});
		RID b = AddNode(graph, "constant_float", Vec4{3.0f, 0.0f, 0.0f, 0.0f});
		RID mul = AddNode(graph, "multiply", Vec4{});
		Connect(graph, a, 0, mul, 0);
		Connect(graph, b, 0, mul, 1);
		Connect(graph, mul, 0, outputNode, MaterialNodeRegistry::Roughness);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		// Sources emit first (n0, n1), then the Multiply temp (n2); the output stays scalar.
		CHECK(Contains(body, "float n2 = (n0 * n1)"));
		CHECK_FALSE(Contains(body, "float3 n2"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenericMathVectorPromotion")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		// Vec3 * float resolves the Multiply node to Vec3; the scalar input is broadcast.
		RID color = AddNode(graph, "constant_color", Vec4{0.5f, 0.5f, 0.5f, 1.0f});
		RID scalar = AddNode(graph, "constant_float", Vec4{2.0f, 0.0f, 0.0f, 0.0f});
		RID mul = AddNode(graph, "multiply", Vec4{});
		Connect(graph, color, 0, mul, 0);
		Connect(graph, scalar, 0, mul, 1);
		Connect(graph, mul, 0, outputNode, MaterialNodeRegistry::Roughness);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		// The Multiply output is float3 and the scalar B operand is broadcast to float3.
		CHECK(Contains(body, "float3 n2 = (n0 * (float3)(n1))"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::ExtendedMathNodes")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		// The Tier 1 math/vector batch is registered.
		for (const char* typeId : {"subtract", "divide", "power", "min", "max", "step", "one_minus",
			 "saturate", "clamp", "smoothstep", "remap", "dot", "normalize", "length"})
		{
			CHECK_MESSAGE(MaterialNodeRegistry::Find(typeId) != nullptr, typeId);
		}

		// Subtract is a generic binary op like Multiply/Add: both pins and the output adapt to the
		// connected type.
		MaterialNode* sub = MaterialNodeRegistry::Find("subtract");
		REQUIRE(sub != nullptr);
		CHECK(sub->GetInputs().Size() == 2);
		CHECK(sub->GetInputs()[0].generic);
		CHECK(sub->GetInputs()[1].generic);
		CHECK(sub->GetOutputs()[0].generic);

		// Dot collapses any vector width to a scalar: inputs are generic but the output is a fixed Float.
		MaterialNode* dot = MaterialNodeRegistry::Find("dot");
		REQUIRE(dot != nullptr);
		CHECK(dot->GetInputs()[0].generic);
		CHECK_FALSE(dot->GetOutputs()[0].generic);
		CHECK(dot->GetOutputs()[0].type == MaterialDataType::Float);

		// Length likewise always yields a scalar.
		MaterialNode* length = MaterialNodeRegistry::Find("length");
		REQUIRE(length != nullptr);
		CHECK_FALSE(length->GetOutputs()[0].generic);
		CHECK(length->GetOutputs()[0].type == MaterialDataType::Float);

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::SubtractCodegen")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		// Single scalar chain: sources emit first (n0, n1), then the Subtract temp (n2) stays scalar.
		RID a = AddNode(graph, "constant_float", Vec4{5.0f, 0.0f, 0.0f, 0.0f});
		RID b = AddNode(graph, "constant_float", Vec4{2.0f, 0.0f, 0.0f, 0.0f});
		RID sub = AddNode(graph, "subtract", Vec4{});
		Connect(graph, a, 0, sub, 0);
		Connect(graph, b, 0, sub, 1);
		Connect(graph, sub, 0, outputNode, MaterialNodeRegistry::Roughness);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		CHECK(Contains(body, "float n2 = (n0 - n1)"));
		CHECK_FALSE(Contains(body, "float3 n2"));
		CHECK(Contains(body, "surface.roughness ="));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::DotCollapsesToScalar")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		// Two Vec3 colors feed a Dot whose output is a fixed Float, so the temp is declared scalar
		// even though both inputs are vectors.
		RID c0 = AddNode(graph, "constant_color", Vec4{1.0f, 0.0f, 0.0f, 1.0f});
		RID c1 = AddNode(graph, "constant_color", Vec4{0.0f, 1.0f, 0.0f, 1.0f});
		RID dot = AddNode(graph, "dot", Vec4{});
		Connect(graph, c0, 0, dot, 0);
		Connect(graph, c1, 0, dot, 1);
		Connect(graph, dot, 0, outputNode, MaterialNodeRegistry::Roughness);

		String log;
		String body = MaterialGraphCompiler::GenerateBody(graph, log);

		CHECK(Contains(body, "float n2 = dot(n0, n1)"));
		CHECK_FALSE(Contains(body, "float3 n2 = dot("));
		CHECK(Contains(body, "surface.roughness ="));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::CompileToSpirv")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		// A texture-free graph: a constant color into Base Color, a clamped scalar into Roughness, and a
		// Dot collapsing two colors to a scalar into Metallic. Bindless texture sampling lands in a later
		// step, so texture nodes are intentionally excluded from the end-to-end compile here.
		RID color = AddNode(graph, "constant_color", Vec4{0.4f, 0.6f, 0.8f, 1.0f});
		Connect(graph, color, 0, outputNode, MaterialNodeRegistry::BaseColor);

		RID half = AddNode(graph, "constant_float", Vec4{0.5f, 0.0f, 0.0f, 0.0f});
		RID clampNode = AddNode(graph, "clamp", Vec4{});
		Connect(graph, half, 0, clampNode, 0);
		Connect(graph, clampNode, 0, outputNode, MaterialNodeRegistry::Roughness);

		RID otherColor = AddNode(graph, "constant_color", Vec4{0.0f, 1.0f, 0.0f, 1.0f});
		RID dotNode = AddNode(graph, "dot", Vec4{});
		Connect(graph, color, 0, dotNode, 0);
		Connect(graph, otherColor, 0, dotNode, 1);
		Connect(graph, dotNode, 0, outputNode, MaterialNodeRegistry::Metallic);

		#ifndef SK_SHADERS_NEW_DIR
		REQUIRE(false);
		#endif

		String shadersDir = String{SK_SHADERS_NEW_DIR};
		String templateText = FileSystem::ReadFileAsString(Path::Join(shadersDir, "ForwardOpaque.raster"));
		REQUIRE(!templateText.Empty());

		String log;
		String hlsl = MaterialGraphCompiler::GenerateShader(graph, templateText, log);

		// the generated body was spliced into the template, replacing the marker
		CHECK(Contains(hlsl, "EvaluateMaterial"));
		CHECK(Contains(hlsl, "surface.baseColor ="));
		CHECK_FALSE(Contains(hlsl, "// @SK_MATERIAL_GRAPH@"));

		// Live SPIR-V compilation needs dxcompiler.dll in the working directory. ShaderManagerInit
		// asserts if it can't be loaded, so only init when the library is actually present.
		bool dxcAvailable = FileSystem::GetFileStatus(Path::Join(FileSystem::CurrentDir(), "dxcompiler.dll")).exists;
		if (dxcAvailable)
		{
			ShaderManagerInit();

			struct IncludeCtx
			{
				String dir;
			};
			IncludeCtx includeCtx{shadersDir};

			struct StageDef
			{
				const char* entryPoint;
				ShaderStage stage;
			};
			const StageDef stageDefs[] = {
				{"MainVS", ShaderStage::Vertex},
				{"MainPS", ShaderStage::Pixel},
			};

			Array<u8> bytes;
			for (const StageDef& stageDef : stageDefs)
			{
				ShaderCompileInfo info;
				info.source = hlsl;
				info.entryPoint = stageDef.entryPoint;
				info.shaderStage = stageDef.stage;
				info.api = GraphicsAPI::Vulkan;
				info.userData = &includeCtx;
				info.getShaderInclude = [](StringView include, void* userData, String& source) -> bool
				{
					const IncludeCtx& ctx = *static_cast<IncludeCtx*>(userData);
					String path = Path::Join(ctx.dir, include);
					if (FileSystem::GetFileStatus(path).exists)
					{
						source = FileSystem::ReadFileAsString(path);
						return true;
					}
					return false;
				};

				String compileLog;
				CHECK_MESSAGE(CompileShader(info, bytes, compileLog), compileLog.CStr());
			}

			CHECK(!bytes.Empty());
			ShaderManagerShutdown();
		}
		else
		{
			MESSAGE("dxcompiler.dll not found in working dir; skipped live SPIR-V compile.");
		}

		ResourceShutdown();
	}
}
