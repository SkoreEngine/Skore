#include "MaterialGraphCompiler.hpp"

#include "MaterialNode.hpp"

#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Utils/ShaderManager.hpp"

#include <cstdio>

namespace Skore
{
	namespace
	{
		String UIntStr(u32 v)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%u", v);
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
	}

	String MaterialGraphCompiler::GenerateHlsl(RID graph, String& log)
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

		String baseColorExpr = "float3(0.8, 0.8, 0.8)";
		String metallicExpr = "0.0";
		String roughnessExpr = "0.5";
		String emissiveExpr = "float3(0.0, 0.0, 0.0)";
		String normalExpr = "float3(0.0, 0.0, 1.0)";
		String occlusionExpr = "1.0";
		String opacityExpr = "1.0";

		if (outputNode && outDef)
		{
			baseColorExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::BaseColor);
			metallicExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Metallic);
			roughnessExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Roughness);
			emissiveExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Emissive);
			normalExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Normal);
			occlusionExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Occlusion);
			opacityExpr = emitter.ResolveInput(outputNode, *outDef, MaterialNodeRegistry::Opacity);
		}
		else
		{
			log += "Material graph has no output node.\n";
		}

		String shader;
		shader += "// Generated by Skore Material Graph. Do not edit by hand.\n\n";
		if (emitter.usedTextures)
		{
			u32 textureCount = emitter.nodeCounter > 0 ? emitter.nodeCounter : 1;
			shader += "Texture2D    MaterialTextures[" + UIntStr(textureCount) + "] : register(t0, space0);\n";
			shader += "SamplerState MaterialSampler : register(s0, space0);\n\n";
		}
		shader += "struct PixelInput\n{\n    float4 position : SV_POSITION;\n    float2 texCoord : TEXCOORD0;\n};\n\n";
		shader += "struct PixelOutput\n{\n    float4 color : SV_TARGET0;\n};\n\n";
		shader += "PixelOutput MainPS(PixelInput input)\n{\n";
		shader += emitter.body;
		if (!emitter.body.Empty())
		{
			shader += "\n";
		}
		shader += "    float3 baseColor = " + baseColorExpr + ";\n";
		shader += "    float  metallic  = " + metallicExpr + ";\n";
		shader += "    float  roughness = " + roughnessExpr + ";\n";
		shader += "    float3 emissive  = " + emissiveExpr + ";\n";
		shader += "    float3 normal    = " + normalExpr + ";\n";
		shader += "    float  occlusion = " + occlusionExpr + ";\n";
		shader += "    float  opacity   = " + opacityExpr + ";\n\n";
		shader += "    float3 surface = lerp(baseColor, baseColor * roughness, metallic) * occlusion + emissive;\n";
		shader += "    surface += normal * 0.0;\n";
		shader += "    PixelOutput output;\n";
		shader += "    output.color = float4(surface, opacity);\n";
		shader += "    return output;\n";
		shader += "}\n";
		return shader;
	}

	MaterialGraphCompileResult MaterialGraphCompiler::Compile(RID graph)
	{
		MaterialGraphCompileResult result;
		result.hlsl = GenerateHlsl(graph, result.log);

		ShaderCompileInfo info;
		info.source = result.hlsl;
		info.entryPoint = "MainPS";
		info.shaderStage = ShaderStage::Pixel;
		info.api = GraphicsAPI::Vulkan;

		Array<u8> bytes;
		String    compileLog;
		bool      ok = CompileShader(info, bytes, compileLog);

		if (!compileLog.Empty())
		{
			result.log += "\n--- Shader Compiler ---\n";
			result.log += compileLog;
		}

		result.success = ok;
		result.spirvSize = static_cast<u32>(bytes.Size());

		if (ok)
		{
			result.log += "\nCompiled successfully (" + UIntStr(static_cast<u32>(bytes.Size())) + " bytes of SPIR-V).\n";
		}

		return result;
	}
}
