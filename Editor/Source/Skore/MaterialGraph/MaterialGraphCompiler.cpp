#include "MaterialGraphCompiler.hpp"

#include "MaterialNode.hpp"

#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Utils/ShaderManager.hpp"

#include <cstdio>
#include <string_view>

namespace Skore
{
	namespace
	{
		constexpr const char* BodyToken = "// @SK_MATERIAL_GRAPH@";
		constexpr const char* VertexBodyToken = "// @SK_MATERIAL_VERTEX_GRAPH@";
		constexpr const char* GlobalsToken = "// @SK_MATERIAL_GLOBALS@";

		String UIntStr(u32 v)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%u", v);
			return String{buf};
		}

		String FltStr(f32 v)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%g", v);
			return String{buf};
		}

		MaterialDataType TypeFromComponentCount(u32 count)
		{
			switch (count)
			{
				case 1:  return MaterialDataType::Float;
				case 2:  return MaterialDataType::Vec2;
				case 3:  return MaterialDataType::Vec3;
				default: return MaterialDataType::Vec4;
			}
		}

		String GenerateParamGlobals(const MaterialParamLayout& layout)
		{
			if (layout.size == 0)
			{
				return "";
			}

			String g;
			g += "ByteAddressBuffer SK_MaterialParamBuffer : register(t7, space2);\n";
			g += "uint   SK_MatParamBase()       { return SK_MaterialIndex * 256u; }\n";
			g += "float  MatParamFloat(uint o)   { return asfloat(SK_MaterialParamBuffer.Load(SK_MatParamBase() + o)); }\n";
			g += "float2 MatParamVec2(uint o)    { return asfloat(SK_MaterialParamBuffer.Load2(SK_MatParamBase() + o)); }\n";
			g += "float3 MatParamVec3(uint o)    { return asfloat(SK_MaterialParamBuffer.Load3(SK_MatParamBase() + o)); }\n";
			g += "float4 MatParamVec4(uint o)    { return asfloat(SK_MaterialParamBuffer.Load4(SK_MatParamBase() + o)); }\n";
			g += "int    MatParamTexture(uint o) { return asint(SK_MaterialParamBuffer.Load(SK_MatParamBase() + o)); }\n";
			g += "uint   MatParamSampler(uint o) { return SK_MaterialParamBuffer.Load(SK_MatParamBase() + o); }\n";
			return g;
		}

		struct ConnRec
		{
			u64 inNode = 0;
			u32 inPin = 0;
			u64 outNode = 0;
			u32 outPin = 0;
		};

		struct Emitter
		{
			HashMap<u64, RID>&          nodeById;
			Array<ConnRec>&             conns;
			String&                     log;
			String                      body{};
			u32                         varCounter = 0;
			u32                         nodeCounter = 0;
			bool                        usedTextures = false;
			bool                        vertexStage = false;
			HashMap<u64, Array<String>>           outVars{};
			HashMap<u64, Array<MaterialDataType>> outTypes{};
			HashSet<u64>                emitting{};
			HashMap<u64, u32>           nodeOffsets{};

			String GetOutputVar(RID node, u32 pin)
			{
				EmitNode(node);
				if (auto it = outVars.Find(node.id); it != outVars.end() && pin < it->second.Size())
				{
					return it->second[pin];
				}
				return "0.0";
			}

			//Effective output type of a node's pin: the type resolved at emit time for generic
			//outputs (memoized in outTypes), or the statically declared pin type otherwise.
			MaterialDataType OutputType(RID node, const MaterialNode& def, u32 pin)
			{
				if (auto it = outTypes.Find(node.id); it != outTypes.end() && pin < it->second.Size())
				{
					return it->second[pin];
				}
				Span<MaterialNodePin> outputs = def.GetOutputs();
				return pin < outputs.Size() ? outputs[pin].type : MaterialDataType::Float;
			}

			//Resolves the concrete type a generic node operates at: the widest (largest component
			//count) type among its connected generic inputs, or Float when none is connected.
			MaterialDataType ResolveGenericType(RID node, const MaterialNode& def)
			{
				Span<MaterialNodePin> inputs = def.GetInputs();
				u32                   maxCount = 0;

				for (u32 i = 0; i < inputs.Size(); ++i)
				{
					if (!inputs[i].generic)
					{
						continue;
					}

					for (const ConnRec& c : conns)
					{
						if (c.inNode != node.id || c.inPin != i)
						{
							continue;
						}

						auto srcIt = nodeById.Find(c.outNode);
						if (srcIt == nodeById.end())
						{
							break;
						}

						RID            srcNode = srcIt->second;
						ResourceObject srcObj = Resources::Read(srcNode);
						if (!srcObj)
						{
							break;
						}

						MaterialNode* srcDef = MaterialNodeRegistry::Find(srcObj.GetString(MaterialGraphNodeResource::Type));
						if (!srcDef || c.outPin >= srcDef->GetOutputs().Size())
						{
							break;
						}

						EmitNode(srcNode);
						u32 count = MaterialComponentCount(OutputType(srcNode, *srcDef, c.outPin));
						if (count > maxCount)
						{
							maxCount = count;
						}
						break;
					}
				}

				return maxCount == 0 ? MaterialDataType::Float : TypeFromComponentCount(maxCount);
			}

			String ResolveInput(RID node, const MaterialNode& def, u32 inputPin, MaterialDataType resolvedType = MaterialDataType::Float)
			{
				Span<MaterialNodePin> inputs = def.GetInputs();
				MaterialDataType      targetType = inputs[inputPin].generic ? resolvedType : inputs[inputPin].type;

				for (const ConnRec& c : conns)
				{
					if (c.inNode != node.id || c.inPin != inputPin)
					{
						continue;
					}

					auto srcIt = nodeById.Find(c.outNode);
					if (srcIt == nodeById.end())
					{
						break;
					}

					RID            srcNode = srcIt->second;
					ResourceObject srcObj = Resources::Read(srcNode);
					if (!srcObj)
					{
						break;
					}

					MaterialNode* srcDef = MaterialNodeRegistry::Find(srcObj.GetString(MaterialGraphNodeResource::Type));
					if (!srcDef || c.outPin >= srcDef->GetOutputs().Size())
					{
						break;
					}

					String           srcVar = GetOutputVar(srcNode, c.outPin);
					MaterialDataType srcType = OutputType(srcNode, *srcDef, c.outPin);
					return MaterialConvertExpr(srcVar, srcType, targetType);
				}

				const MaterialNodePin& pin = inputs[inputPin];
				if (!pin.defaultExpr.Empty())
				{
					return pin.defaultExpr;
				}
				return MaterialLiteralExpr(targetType, MaterialReadPinValue(node, inputPin, pin.defaultValue));
			}

			void EmitNode(RID node)
			{
				if (outVars.Has(node.id))
				{
					return;
				}

				if (emitting.Has(node.id))
				{
					log += "Cycle detected in material graph; node skipped.\n";
					return;
				}

				ResourceObject obj = Resources::Read(node);
				if (!obj)
				{
					return;
				}

				StringView    typeId = obj.GetString(MaterialGraphNodeResource::Type);
				MaterialNode* def = MaterialNodeRegistry::Find(typeId);
				if (!def)
				{
					log += String{"Unknown node type: "} + typeId + "\n";
					return;
				}

				if (def->IsOutput())
				{
					return;
				}

				emitting.Insert(node.id);

				Span<MaterialNodePin> inputs = def->GetInputs();
				Span<MaterialNodePin> outputs = def->GetOutputs();

				MaterialDataType resolvedType = ResolveGenericType(node, *def);

				Array<String> inputExprs;
				inputExprs.Resize(inputs.Size());
				for (u32 i = 0; i < inputs.Size(); ++i)
				{
					inputExprs[i] = ResolveInput(node, *def, i, resolvedType);
				}

				Array<String> outputExprs;
				outputExprs.Resize(outputs.Size());

				Array<MaterialDataType> varTypes;
				varTypes.Resize(outputs.Size());
				for (u32 o = 0; o < outputs.Size(); ++o)
				{
					varTypes[o] = outputs[o].generic ? resolvedType : outputs[o].type;
				}

				Array<String> statements;
				u32           nodeIndex = nodeCounter++;

				MaterialCodegenContext ctx{Span<String>(inputExprs), obj.GetVec4(MaterialGraphNodeResource::Value), nodeIndex, outputExprs, statements, varTypes, &usedTextures};
				ctx.vertexStage = vertexStage;
				if (auto it = nodeOffsets.Find(node.id); it != nodeOffsets.end())
				{
					ctx.paramByteOffset = it->second;
				}
				def->Generate(ctx);

				for (const String& statement : statements)
				{
					body += String{"    "} + statement + "\n";
				}

				Array<String> varNames;
				varNames.Resize(outputs.Size());
				for (u32 o = 0; o < outputs.Size(); ++o)
				{
					String varName = "n" + UIntStr(varCounter++);
					varNames[o] = varName;
					body += String{"    "} + MaterialHlslType(varTypes[o]) + " " + varName + " = " + outputExprs[o] + ";\n";
				}

				emitting.Erase(node.id);
				outTypes.Insert(node.id, Traits::Move(varTypes));
				outVars.Insert(node.id, Traits::Move(varNames));
			}
		};

		struct GraphTopology
		{
			HashMap<u64, RID> nodeById{};
			Array<ConnRec>    conns{};
			RID               outputNode{};
		};

		void LoadGraphTopology(ResourceObject& graphObj, GraphTopology& topo)
		{
			Span<RID> nodes = graphObj.GetSubObjectList(MaterialGraphResource::Nodes);
			Span<RID> conns = graphObj.GetSubObjectList(MaterialGraphResource::Connections);

			for (RID n : nodes)
			{
				topo.nodeById.Insert(n.id, n);
			}

			for (RID c : conns)
			{
				ResourceObject co = Resources::Read(c);
				if (!co)
				{
					continue;
				}
				ConnRec rec;
				rec.outNode = co.GetReference(MaterialGraphConnectionResource::OutputNode).id;
				rec.outPin = static_cast<u32>(co.GetUInt(MaterialGraphConnectionResource::OutputPin));
				rec.inNode = co.GetReference(MaterialGraphConnectionResource::InputNode).id;
				rec.inPin = static_cast<u32>(co.GetUInt(MaterialGraphConnectionResource::InputPin));
				topo.conns.EmplaceBack(rec);
			}

			topo.outputNode = graphObj.GetReference(MaterialGraphResource::OutputNode);
			if (!topo.outputNode)
			{
				for (RID n : nodes)
				{
					ResourceObject no = Resources::Read(n);
					if (no && no.GetString(MaterialGraphNodeResource::Type) == MaterialNodeRegistry::OutputTypeId())
					{
						topo.outputNode = n;
						break;
					}
				}
			}
		}

		//Replaces the first occurrence of `token` in `templateText` with `replacement`; returns the
		//template unchanged when the token is absent.
		String SpliceToken(StringView templateText, const char* token, const String& replacement)
		{
			std::string_view text{templateText.CStr(), templateText.Size()};
			std::string_view tok{token};

			usize pos = text.find(tok);
			if (pos == std::string_view::npos)
			{
				return String{templateText.CStr(), templateText.Size()};
			}

			usize  tail = pos + tok.size();
			String result{text.data(), pos};
			result += replacement;
			result += String{text.data() + tail, text.size() - tail};
			return result;
		}

		//Resolves the splice-template HLSL source behind a shader asset file. `.shader` configs are YAML
		//wrappers, so the template is the file their `shaderFile` field points at; any other extension is
		//the source itself. `absPathOut` receives the source path so relative includes resolve from there.
		String ReadTemplateSource(StringView assetAbsPath, String& absPathOut)
		{
			if (Path::Extension(assetAbsPath) == ".shader")
			{
				String yaml = FileSystem::ReadFileAsString(assetAbsPath);
				if (yaml.Empty())
				{
					return "";
				}

				YamlArchiveReader reader(yaml);
				StringView        shaderFile = reader.ReadString("shaderFile");
				if (shaderFile.Empty())
				{
					return "";
				}

				String sourcePath = Path::Join(Path::Parent(assetAbsPath), shaderFile);
				if (!FileSystem::GetFileStatus(sourcePath).exists)
				{
					return "";
				}
				absPathOut = sourcePath;
				return FileSystem::ReadFileAsString(absPathOut);
			}

			absPathOut = assetAbsPath;
			return FileSystem::ReadFileAsString(absPathOut);
		}

		String ReadShaderTemplate(RID shader, String& absPathOut)
		{
			if (!shader)
			{
				return "";
			}
			RID asset = ResourceAssets::GetResourceAssetFromResourceRecursive(shader);
			if (!asset)
			{
				return "";
			}
			StringView abs = ResourceAssets::GetAbsolutePath(asset);
			if (abs.Empty() || !FileSystem::GetFileStatus(abs).exists)
			{
				return "";
			}
			return ReadTemplateSource(abs, absPathOut);
		}

		//Default template when no specific shader is in play: the first shader asset flagged material
		//(`material: true` in its `.shader` config).
		String LoadTemplate(String& absPathOut, String& log)
		{
			for (RID shader : Resources::GetResourcesByType(Resources::FindType<ShaderResource>()))
			{
				if (!ShaderResource::IsMaterialShader(shader))
				{
					continue;
				}
				String templateText = ReadShaderTemplate(shader, absPathOut);
				if (!templateText.Empty())
				{
					return templateText;
				}
			}
			log += "Material graph template not found: no shader asset is flagged as material.\n";
			return "";
		}

		//Any shader flagged IsMaterial can host a graph: its own source is the splice template. Falls back
		//to the default material template when the shader has no resolvable source file.
		String LoadShaderTemplate(RID shader, String& absPathOut, String& log)
		{
			String templateText = ReadShaderTemplate(shader, absPathOut);
			if (!templateText.Empty())
			{
				return templateText;
			}
			return LoadTemplate(absPathOut, log);
		}

		struct TemplateIncludeUserData
		{
			StringView baseAbsPath;
		};

		bool ResolveTemplateInclude(StringView include, void* userData, String& source)
		{
			const TemplateIncludeUserData& ud = *static_cast<TemplateIncludeUserData*>(userData);

			std::string_view inc{include.CStr(), include.Size()};
			if (inc.find(":/") != std::string_view::npos)
			{
				String abs = ResourceAssets::GetAbsolutePathFromPathId(include);
				if (!abs.Empty() && FileSystem::GetFileStatus(abs).exists)
				{
					source = FileSystem::ReadFileAsString(abs);
					return true;
				}
			}

			String joined = Path::Join(Path::Parent(ud.baseAbsPath), include);
			if (FileSystem::GetFileStatus(joined).exists)
			{
				source = FileSystem::ReadFileAsString(joined);
				return true;
			}
			return false;
		}

		RID BuildMaterialVariant(StringView templateText, StringView templateAbsPath, RID graph, StringView variantName, RID materialRef, String& log)
		{
			String hlsl = MaterialGraphCompiler::GenerateShader(graph, templateText, log);

			Array<ShaderEntryPoint> entryPoints = DetectShaderStages(templateText);
			if (entryPoints.Empty())
			{
				log += "Material template declares no shader stages (MainVS/MainPS/MainCS or ray-tracing hit shaders).\n";
				return {};
			}

			TemplateIncludeUserData userData{templateAbsPath};

			Array<u8>              bytes;
			Array<ShaderStageInfo> stages;
			u32                    stageOffset = 0;

			for (const ShaderEntryPoint& entryPoint : entryPoints)
			{
				ShaderCompileInfo info;
				info.source = hlsl;
				info.entryPoint = entryPoint.entryPoint;
				info.shaderStage = entryPoint.stage;
				info.macros = entryPoint.macros;
				info.api = GraphicsAPI::Vulkan;
				info.userData = &userData;
				info.getShaderInclude = ResolveTemplateInclude;

				String stageLog;
				if (!CompileShader(info, bytes, stageLog))
				{
					log += stageLog;
					return {};
				}

				u32 stageSize = static_cast<u32>(bytes.Size()) - stageOffset;
				stages.EmplaceBack(ShaderStageInfo{
					.stage = entryPoint.stage,
					.entryPoint = entryPoint.entryPoint,
					.offset = stageOffset,
					.size = stageSize,
					.hitGroup = entryPoint.hitGroup,
				});
				stageOffset += stageSize;
			}

			PipelineDesc pipelineDesc;
			GetPipelineLayout(GraphicsAPI::Vulkan, bytes, stages, pipelineDesc);

			RID pipelineDescRID = Resources::Create<PipelineDesc>(UUID::RandomUUID());
			Resources::ToResource(pipelineDescRID, &pipelineDesc, nullptr);

			RID shaderVariant = Resources::Create<ShaderVariantResource>(UUID::RandomUUID());
			{
				ResourceObject variantObject = Resources::Write(shaderVariant);
				variantObject.SetString(ShaderVariantResource::Name, variantName);
				variantObject.SetBlob(ShaderVariantResource::Spriv, bytes);
				variantObject.SetSubObject(ShaderVariantResource::PipelineDesc, pipelineDescRID);
				if (materialRef)
				{
					variantObject.SetReference(ShaderVariantResource::Material, materialRef);
				}

				for (const ShaderStageInfo& stage : stages)
				{
					RID stageRID = Resources::Create<ShaderStageInfo>(UUID::RandomUUID());
					Resources::ToResource(stageRID, &stage, nullptr);
					variantObject.AddToSubObjectList(ShaderVariantResource::Stages, stageRID);
				}

				variantObject.Commit();
			}

			return shaderVariant;
		}

		RID ResolveOwningGraph(RID material)
		{
			constexpr u32 maxInstanceParentDepth = 16;
			RID           graph = material;
			for (u32 depth = 0; graph && depth < maxInstanceParentDepth; ++depth)
			{
				ResourceObject materialObject = Resources::Read(graph);
				if (!materialObject)
				{
					return material;
				}
				if (materialObject.GetEnum<MaterialGraphResource::MaterialKind>(MaterialGraphResource::Kind) != MaterialGraphResource::MaterialKind::Instance)
				{
					return graph;
				}
				graph = materialObject.GetReference(MaterialGraphResource::Parent);
			}
			return material;
		}
	}

	String MaterialGraphCompiler::GenerateBody(RID graph, String& log)
	{
		ResourceObject graphObj = Resources::Read(graph);
		if (!graphObj)
		{
			log += "Invalid material graph resource.\n";
			return "";
		}

		GraphTopology topo;
		LoadGraphTopology(graphObj, topo);
		RID outputNode = topo.outputNode;

		{
			HashMap<String, String> paramTypes;
			for (RID n : graphObj.GetSubObjectList(MaterialGraphResource::Nodes))
			{
				ResourceObject no = Resources::Read(n);
				if (!no)
				{
					continue;
				}
				String        typeId = String{no.GetString(MaterialGraphNodeResource::Type)};
				MaterialNode* def = MaterialNodeRegistry::Find(typeId);
				if (!def || !def->IsParameter())
				{
					continue;
				}
				String name = String{no.GetString(MaterialGraphNodeResource::ParameterName)};
				if (name.Empty())
				{
					log += String{"Parameter node ("} + typeId + ") has no name; it cannot be overridden by material instances.\n";
					continue;
				}
				if (auto it = paramTypes.Find(name); it != paramTypes.end())
				{
					if (it->second != typeId)
					{
						log += String{"Duplicate parameter name '"} + name + "' is used by different parameter types (" + it->second + " and " + typeId + ").\n";
					}
				}
				else
				{
					paramTypes.Insert(Traits::Move(name), Traits::Move(typeId));
				}
			}

			if (outputNode)
			{
				bool anyIntoOutput = false;
				for (const ConnRec& rec : topo.conns)
				{
					if (rec.inNode == outputNode.id)
					{
						anyIntoOutput = true;
						break;
					}
				}
				if (!anyIntoOutput)
				{
					log += "Nothing is connected to the material output node; default surface values will be used.\n";
				}
			}
		}

		MaterialNode* outDef = MaterialNodeRegistry::Find(MaterialNodeRegistry::OutputTypeId());

		Emitter emitter{topo.nodeById, topo.conns, log};
		emitter.nodeOffsets = MaterialParamLayout::Build(graph).nodeOffsets;

		MaterialGraphResource::GraphAlphaMode alphaMode = graphObj.GetEnum<MaterialGraphResource::GraphAlphaMode>(MaterialGraphResource::AlphaMode);
		f32                                   maskCutoff = graphObj.GetFloat(MaterialGraphResource::MaskCutoff);
		bool                                  unlit = graphObj.GetEnum<MaterialGraphResource::GraphShadingModel>(MaterialGraphResource::ShadingModel) == MaterialGraphResource::GraphShadingModel::Unlit;

		String baseColorExpr = "float3(0.8, 0.8, 0.8)";
		String metallicExpr = "0.0";
		String roughnessExpr = "0.5";
		String emissiveExpr = "float3(0.0, 0.0, 0.0)";
		String normalExpr = "float3(0.0, 0.0, 1.0)";
		String occlusionExpr = "1.0";
		String opacityExpr = "1.0";
		String maskExpr = "1.0";

		if (outputNode && outDef)
		{
			baseColorExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::BaseColor);

			//Unlit only consumes base color (plus the alpha-mode pin below); the lit surface inputs keep
			//their defaults so their subgraphs are never emitted.
			if (!unlit)
			{
				metallicExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Metallic);
				roughnessExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Roughness);
				emissiveExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Emissive);
				normalExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Normal);
				occlusionExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Occlusion);
			}

			//Only the opacity pin relevant to the alpha mode is read; the other stays disconnected in the
			//editor. Opaque reads neither and forces a solid surface.
			if (alphaMode == MaterialGraphResource::GraphAlphaMode::Blend)
			{
				opacityExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Opacity);
			}
			else if (alphaMode == MaterialGraphResource::GraphAlphaMode::Mask)
			{
				maskExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::OpacityMask);
			}
		}
		else
		{
			log += "Material graph has no output node.\n";
		}

		String body = emitter.body;
		if (!body.Empty())
		{
			body += "\n";
		}
		body += "    surface.baseColor = " + baseColorExpr + ";\n";
		body += "    surface.metallic  = " + metallicExpr + ";\n";
		body += "    surface.roughness = " + roughnessExpr + ";\n";
		body += "    surface.emissive  = " + emissiveExpr + ";\n";
		body += "    surface.normal    = " + normalExpr + ";\n";
		body += "    surface.occlusion = " + occlusionExpr + ";\n";

		if (alphaMode == MaterialGraphResource::GraphAlphaMode::Mask)
		{
			body += "    if ((" + maskExpr + ") < " + FltStr(maskCutoff) + ") discard;\n";
			body += "    surface.opacity   = 1.0;\n";
		}
		else if (alphaMode == MaterialGraphResource::GraphAlphaMode::Blend)
		{
			body += "    surface.opacity   = " + opacityExpr + ";\n";
		}
		else
		{
			body += "    surface.opacity   = 1.0;\n";
		}
		return body;
	}

	String MaterialGraphCompiler::GenerateVertexBody(RID graph, String& log)
	{
		ResourceObject graphObj = Resources::Read(graph);
		if (!graphObj)
		{
			return "";
		}

		GraphTopology topo;
		LoadGraphTopology(graphObj, topo);
		if (!topo.outputNode)
		{
			return "";
		}

		bool wpoConnected = false;
		for (const ConnRec& rec : topo.conns)
		{
			if (rec.inNode == topo.outputNode.id && rec.inPin == MaterialNodeRegistry::WorldPositionOffset)
			{
				wpoConnected = true;
				break;
			}
		}
		if (!wpoConnected)
		{
			return "";
		}

		MaterialNode* outDef = MaterialNodeRegistry::Find(MaterialNodeRegistry::OutputTypeId());
		if (!outDef)
		{
			return "";
		}

		Emitter emitter{topo.nodeById, topo.conns, log};
		emitter.vertexStage = true;
		emitter.nodeOffsets = MaterialParamLayout::Build(graph).nodeOffsets;

		String offsetExpr = emitter.ResolveInput(topo.outputNode, *outDef, MaterialNodeRegistry::WorldPositionOffset);

		String body = emitter.body;
		if (!body.Empty())
		{
			body += "\n";
		}
		body += "    worldPositionOffset = " + offsetExpr + ";\n";
		return body;
	}

	String MaterialGraphCompiler::GenerateShader(RID graph, StringView templateText, String& log)
	{
		String body = GenerateBody(graph, log);
		String vertexBody = GenerateVertexBody(graph, log);
		String globals = GenerateParamGlobals(MaterialParamLayout::Build(graph));

		if (ResourceObject graphObj = Resources::Read(graph);
			graphObj && graphObj.GetEnum<MaterialGraphResource::GraphShadingModel>(MaterialGraphResource::ShadingModel) == MaterialGraphResource::GraphShadingModel::Unlit)
		{
			globals = String{"#define SK_MATERIAL_UNLIT 1\n"} + globals;
		}

		String result = SpliceToken(templateText, GlobalsToken, globals);
		result = SpliceToken(result, BodyToken, body);
		return SpliceToken(result, VertexBodyToken, vertexBody);
	}

	MaterialGraphCompileResult MaterialGraphCompiler::Compile(RID graph)
	{
		MaterialGraphCompileResult result;

		String absPath;
		String templateText = LoadTemplate(absPath, result.log);
		if (templateText.Empty())
		{
			result.hlsl = GenerateBody(graph, result.log);
			return result;
		}

		result.hlsl = GenerateShader(graph, templateText, result.log);

		TemplateIncludeUserData userData{absPath};

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
		bool      ok = true;

		for (const StageDef& stageDef : stageDefs)
		{
			ShaderCompileInfo info;
			info.source = result.hlsl;
			info.entryPoint = stageDef.entryPoint;
			info.shaderStage = stageDef.stage;
			info.api = GraphicsAPI::Vulkan;
			info.userData = &userData;
			info.getShaderInclude = ResolveTemplateInclude;

			String stageLog;
			if (!CompileShader(info, bytes, stageLog))
			{
				ok = false;
			}
			if (!stageLog.Empty())
			{
				result.log += String{"\n--- Shader Compiler ("} + stageDef.entryPoint + ") ---\n";
				result.log += stageLog;
			}
		}

		result.success = ok;
		result.spirvSize = static_cast<u32>(bytes.Size());

		if (ok)
		{
			result.log += "\nCompiled successfully (" + UIntStr(static_cast<u32>(bytes.Size())) + " bytes of SPIR-V).\n";
		}

		return result;
	}

	RID MaterialGraphCompiler::EnsureMaterialVariant(RID shader, RID material, StringView variantName, String& log)
	{
		if (!shader || !material)
		{
			return {};
		}

		struct VariantState
		{
			u64 version;
			u64 hlslHash;
		};
		static HashMap<u64, VariantState> variantStates;

		RID graph = ResolveOwningGraph(material);
		u64 graphVersion = Resources::GetVersion(graph);

		RID existing = ShaderResource::GetVariant(shader, graph, variantName);
		if (existing)
		{
			if (auto it = variantStates.Find(existing.id); it != variantStates.end() && it->second.version == graphVersion)
			{
				return existing;
			}
		}

		String absPath;
		String templateText = LoadShaderTemplate(shader, absPath, log);
		if (templateText.Empty())
		{
			return existing;
		}

		String hlsl = GenerateShader(graph, templateText, log);
		u64    hlslHash = 14695981039346656037ull;
		for (usize i = 0; i < hlsl.Size(); ++i)
		{
			hlslHash ^= static_cast<u8>(hlsl.CStr()[i]);
			hlslHash *= 1099511628211ull;
		}

		if (existing)
		{
			if (auto it = variantStates.Find(existing.id); it != variantStates.end() && it->second.hlslHash == hlslHash)
			{
				it->second.version = graphVersion;
				return existing;
			}

			ResourceObject shaderObject = Resources::Write(shader);
			shaderObject.RemoveFromSubObjectList(ShaderResource::Variants, existing);
			shaderObject.Commit();
			variantStates.Erase(existing.id);
		}

		RID shaderVariant = BuildMaterialVariant(templateText, absPath, graph, variantName, graph, log);
		if (!shaderVariant)
		{
			return {};
		}

		{
			ResourceObject shaderObject = Resources::Write(shader);
			shaderObject.AddToSubObjectList(ShaderResource::Variants, shaderVariant);
			shaderObject.Commit();
		}

		variantStates.Insert(shaderVariant.id, VariantState{graphVersion, hlslHash});
		return shaderVariant;
	}

	void RegisterMaterialVariantResolver()
	{
		RenderResourceCache::SetMaterialVariantResolver([](RID shader, RID material, StringView variantName) -> RID
		{
			String log;
			return MaterialGraphCompiler::EnsureMaterialVariant(shader, material, variantName, log);
		});
	}
}
