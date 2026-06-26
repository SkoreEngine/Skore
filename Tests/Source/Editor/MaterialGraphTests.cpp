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
		CHECK(output->GetInputs().Size() == 7);
		CHECK(output->GetOutputs().Size() == 0);
		CHECK(output->GetInputs()[MaterialNodeRegistry::BaseColor].type == MaterialDataType::Vec3);
		CHECK(output->GetInputs()[MaterialNodeRegistry::Metallic].type == MaterialDataType::Float);
		CHECK(output->GetInputs()[MaterialNodeRegistry::Normal].type == MaterialDataType::Vec3);
		CHECK(output->GetInputs()[MaterialNodeRegistry::Opacity].type == MaterialDataType::Float);

		MaterialNode* sampleTexture = MaterialNodeRegistry::Find("sample_texture");
		REQUIRE(sampleTexture != nullptr);
		CHECK(sampleTexture->GetInputs().Size() == 1);
		CHECK(sampleTexture->GetOutputs()[0].type == MaterialDataType::Vec4);

		CHECK(MaterialNodeRegistry::Find("normal_map") != nullptr);
		CHECK(MaterialNodeRegistry::Find("tiling_offset") != nullptr);

		MaterialNode* multiply = MaterialNodeRegistry::Find("multiply");
		REQUIRE(multiply != nullptr);
		CHECK_FALSE(multiply->IsOutput());
		CHECK(multiply->GetInputs().Size() == 2);
		CHECK(multiply->GetOutputs().Size() == 1);
		CHECK(multiply->GetOutputs()[0].type == MaterialDataType::Vec3);

		MaterialNode* constantFloat = MaterialNodeRegistry::Find("constant_float");
		REQUIRE(constantFloat != nullptr);
		CHECK(constantFloat->GetValueKind() == MaterialNodeValueKind::Float);
		CHECK(constantFloat->GetOutputs()[0].type == MaterialDataType::Float);

		MaterialNode* color = MaterialNodeRegistry::Find("constant_color");
		REQUIRE(color != nullptr);
		CHECK(color->GetValueKind() == MaterialNodeValueKind::Color);

		MaterialNode* texCoord = MaterialNodeRegistry::Find("tex_coord");
		REQUIRE(texCoord != nullptr);
		CHECK(texCoord->GetOutputs()[0].type == MaterialDataType::Vec2);

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

	TEST_CASE("MaterialGraph::GenerateHlslDefaults")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		String log;
		String hlsl = MaterialGraphCompiler::GenerateHlsl(graph, log);

		CHECK(!hlsl.Empty());
		CHECK(Contains(hlsl, "PixelOutput MainPS"));
		// unconnected output inputs fall back to their pin defaults
		CHECK(Contains(hlsl, "float3 baseColor = float3(0.8, 0.8, 0.8)"));
		CHECK(Contains(hlsl, "float  roughness = 0.5"));
		// the expanded master node emits normal / occlusion / opacity surface fields
		CHECK(Contains(hlsl, "float3 normal"));
		CHECK(Contains(hlsl, "float  occlusion"));
		CHECK(Contains(hlsl, "float  opacity"));
		CHECK(Contains(hlsl, "float3(0, 0, 1)")); // tangent-space normal default

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateHlslTexture")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);

		RID texNode = AddNode(graph, "sample_texture", Vec4{});
		Connect(graph, texNode, 0, outputNode, MaterialNodeRegistry::BaseColor);

		String log;
		String hlsl = MaterialGraphCompiler::GenerateHlsl(graph, log);

		// bindless texture preamble is emitted only when a texture node is present
		CHECK(Contains(hlsl, "Texture2D    MaterialTextures["));
		CHECK(Contains(hlsl, "SamplerState MaterialSampler"));
		CHECK(Contains(hlsl, ".Sample(MaterialSampler,"));
		// unconnected UV falls back to the mesh UVs via the pin's default expression
		CHECK(Contains(hlsl, "input.texCoord"));
		// the Vec4 sample feeding a Vec3 input is swizzled down
		CHECK(Contains(hlsl, ".xyz"));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateHlslConnected")
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
		String hlsl = MaterialGraphCompiler::GenerateHlsl(graph, log);

		CHECK(Contains(hlsl, "float3 n"));            // a temp var was emitted for an upstream node
		CHECK(Contains(hlsl, "float3(1, 0, 0)"));     // the color literal
		CHECK(Contains(hlsl, "0.25"));                // the roughness literal
		CHECK(Contains(hlsl, "baseColor ="));
		CHECK(Contains(hlsl, "roughness ="));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::GenerateHlslTypeConversion")
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
		String hlsl = MaterialGraphCompiler::GenerateHlsl(graph, log);

		CHECK(Contains(hlsl, "(float3)("));

		ResourceShutdown();
	}

	TEST_CASE("MaterialGraph::CompileToSpirv")
	{
		ResourceInit();
		SetupMaterialGraphTypes();
		RegisterMaterialNodes();

		RID outputNode;
		RID graph = NewGraph(outputNode);
		// Exercise the bindless texture preamble, Sample(), the normal-map helper statement, and the
		// new Normal output — all the way through real DXC → SPIR-V.
		RID texNode = AddNode(graph, "sample_texture", Vec4{});
		Connect(graph, texNode, 0, outputNode, MaterialNodeRegistry::BaseColor);
		RID normalNode = AddNode(graph, "normal_map", Vec4{});
		Connect(graph, normalNode, 0, outputNode, MaterialNodeRegistry::Normal);

		// Live SPIR-V compilation needs dxcompiler.dll in the working directory. ShaderManagerInit
		// asserts if it can't be loaded, so only init when the library is actually present.
		bool dxcAvailable = FileSystem::GetFileStatus(Path::Join(FileSystem::CurrentDir(), "dxcompiler.dll")).exists;
		if (dxcAvailable)
		{
			ShaderManagerInit();
		}

		MaterialGraphCompileResult result = MaterialGraphCompiler::Compile(graph);

		// HLSL generation is deterministic regardless of the shader compiler.
		CHECK(!result.hlsl.Empty());
		CHECK(Contains(result.hlsl, "PixelOutput MainPS"));

		if (dxcAvailable)
		{
			CHECK_MESSAGE(result.success, result.log.CStr());
			CHECK(result.spirvSize > 0);
			ShaderManagerShutdown();
		}
		else
		{
			MESSAGE("dxcompiler.dll not found in working dir; skipped live SPIR-V compile.");
		}

		ResourceShutdown();
	}
}
