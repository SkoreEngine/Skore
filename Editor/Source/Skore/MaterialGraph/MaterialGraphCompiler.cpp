#include "MaterialGraphCompiler.hpp"

#include "MaterialNode.hpp"

#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
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
		constexpr const char* TemplatePathId = "Skore://ShadersNew/MaterialGraphForward.template.raster";

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
			HashMap<u64, Array<String>>           outVars{};
			HashMap<u64, Array<MaterialDataType>> outTypes{};
			HashSet<u64>                emitting{};

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

				Array<String> statements;
				u32           nodeIndex = nodeCounter++;

				MaterialCodegenContext ctx{Span<String>(inputExprs), obj.GetVec4(MaterialGraphNodeResource::Value), nodeIndex, outputExprs, statements, &usedTextures};
				def->Generate(ctx);

				for (const String& statement : statements)
				{
					body += String{"    "} + statement + "\n";
				}

				Array<String>           varNames;
				Array<MaterialDataType> varTypes;
				varNames.Resize(outputs.Size());
				varTypes.Resize(outputs.Size());
				for (u32 o = 0; o < outputs.Size(); ++o)
				{
					MaterialDataType outType = outputs[o].generic ? resolvedType : outputs[o].type;
					String           varName = "n" + UIntStr(varCounter++);
					varNames[o] = varName;
					varTypes[o] = outType;
					body += String{"    "} + MaterialHlslType(outType) + " " + varName + " = " + outputExprs[o] + ";\n";
				}

				emitting.Erase(node.id);
				outTypes.Insert(node.id, Traits::Move(varTypes));
				outVars.Insert(node.id, Traits::Move(varNames));
			}
		};

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

		//Reads the runtime template from Skore://; sets `absPathOut` to the resolved on-disk path so the
		//shader compiler can resolve the template's relative includes from the same folder.
		String LoadTemplate(String& absPathOut, String& log)
		{
			absPathOut = ResourceAssets::GetAbsolutePathFromPathId(TemplatePathId);
			if (absPathOut.Empty() || !FileSystem::GetFileStatus(absPathOut).exists)
			{
				log += String{"Material graph template not found: "} + TemplatePathId + "\n";
				return "";
			}
			return FileSystem::ReadFileAsString(absPathOut);
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
	}

	String MaterialGraphCompiler::GenerateBody(RID graph, String& log)
	{
		ResourceObject graphObj = Resources::Read(graph);
		if (!graphObj)
		{
			log += "Invalid material graph resource.\n";
			return "";
		}

		Span<RID> nodes = graphObj.GetSubObjectList(MaterialGraphResource::Nodes);
		Span<RID> conns = graphObj.GetSubObjectList(MaterialGraphResource::Connections);

		HashMap<u64, RID> nodeById;
		for (RID n : nodes)
		{
			nodeById.Insert(n.id, n);
		}

		Array<ConnRec> connRecs;
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
			connRecs.EmplaceBack(rec);
		}

		RID outputNode = graphObj.GetReference(MaterialGraphResource::OutputNode);
		if (!outputNode)
		{
			for (RID n : nodes)
			{
				ResourceObject no = Resources::Read(n);
				if (no && no.GetString(MaterialGraphNodeResource::Type) == MaterialNodeRegistry::OutputTypeId())
				{
					outputNode = n;
					break;
				}
			}
		}

		MaterialNode* outDef = MaterialNodeRegistry::Find(MaterialNodeRegistry::OutputTypeId());

		Emitter emitter{nodeById, connRecs, log};

		MaterialGraphResource::GraphAlphaMode alphaMode = graphObj.GetEnum<MaterialGraphResource::GraphAlphaMode>(MaterialGraphResource::AlphaMode);
		f32                                   maskCutoff = graphObj.GetFloat(MaterialGraphResource::MaskCutoff);

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
			metallicExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Metallic);
			roughnessExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Roughness);
			emissiveExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Emissive);
			normalExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Normal);
			occlusionExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Occlusion);

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

	String MaterialGraphCompiler::GenerateShader(RID graph, StringView templateText, String& log)
	{
		String body = GenerateBody(graph, log);
		return SpliceToken(templateText, BodyToken, body);
	}

	String MaterialGraphCompiler::GenerateHlsl(RID graph, String& log)
	{
		String absPath;
		String templateText = LoadTemplate(absPath, log);
		if (templateText.Empty())
		{
			return GenerateBody(graph, log);
		}
		return GenerateShader(graph, templateText, log);
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

	RID MaterialGraphCompiler::CompileToShaderResource(RID graph, String& log)
	{
		String absPath;
		String templateText = LoadTemplate(absPath, log);
		if (templateText.Empty())
		{
			return {};
		}

		String hlsl = GenerateShader(graph, templateText, log);

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

		Array<u8>              bytes;
		Array<ShaderStageInfo> stages;
		u32                    stageOffset = 0;

		for (const StageDef& stageDef : stageDefs)
		{
			ShaderCompileInfo info;
			info.source = hlsl;
			info.entryPoint = stageDef.entryPoint;
			info.shaderStage = stageDef.stage;
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
				.stage = stageDef.stage,
				.entryPoint = String{stageDef.entryPoint},
				.offset = stageOffset,
				.size = stageSize,
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
			variantObject.SetString(ShaderVariantResource::Name, "Default");
			variantObject.SetBlob(ShaderVariantResource::Spriv, bytes);
			variantObject.SetSubObject(ShaderVariantResource::PipelineDesc, pipelineDescRID);

			for (const ShaderStageInfo& stage : stages)
			{
				RID stageRID = Resources::Create<ShaderStageInfo>(UUID::RandomUUID());
				Resources::ToResource(stageRID, &stage, nullptr);
				variantObject.AddToSubObjectList(ShaderVariantResource::Stages, stageRID);
			}

			variantObject.Commit();
		}

		RID shaderResource = Resources::Create<ShaderResource>(UUID::RandomUUID());
		{
			ResourceObject shaderObject = Resources::Write(shaderResource);
			shaderObject.SetString(ShaderResource::Name, "MaterialGraph");
			shaderObject.AddToSubObjectList(ShaderResource::Variants, shaderVariant);
			shaderObject.Commit();
		}

		return shaderResource;
	}
}
