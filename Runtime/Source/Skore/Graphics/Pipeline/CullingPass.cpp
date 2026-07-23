#include "PipelineCommon.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	namespace
	{
		constexpr const char* OpaqueCountsName = "OpaqueIndirectCounts";
		constexpr const char* OpaqueOffsetsName = "OpaqueCullOffsets";
	}

	//GPU-driven opaque culling: one compute dispatch frustum-culls and LOD-selects every opaque
	//instance into a single flat indirect-draw buffer (per-pipeline segments via drawOffsets) plus a
	//per-pipeline count buffer. Barriers come entirely from the declared buffer dependencies:
	//fill (CopyDest) -> cull (General) -> shader-less read pass (ShaderReadOnly + indirect access).
	struct CullingPass : DefaultPipelinePass
	{
		SK_CLASS(CullingPass, DefaultPipelinePass);

		u32    drawsGeneration = 0;
		u64    drawsCapacity = 0;
		String drawsName;

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			SceneCullingData* data = renderGraph.CreateInstance<SceneCullingData>(SceneCullingDataName);
			data->indirectDrawBuffer = nullptr;
			data->countBuffer = nullptr;
			data->pipelineCount = 0;

			Scene*            scene = renderGraph.GetScene();
			GPUDescriptorSet* sceneSet = renderGraph.GetSceneDescriptorSet();
			GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
			if (scene == nullptr || sceneSet == nullptr || globalSet == nullptr) return;

			RenderSceneObjects* objects = &scene->renderObjects;
			if (objects->instanceDataCount == 0) return;

			u32 pipelineCount = Math::Min(static_cast<u32>(objects->opaquePipelines.Size()), MaxCullingPipelines);

			u32 totalDraws = 0;
			for (u32 i = 0; i < pipelineCount; ++i)
			{
				u32 drawcallCount = static_cast<u32>(objects->opaquePipelines[i].drawcalls.Size());
				data->drawOffsets[i] = totalDraws;
				data->maxDraws[i] = drawcallCount;
				totalDraws += drawcallCount;
			}
			for (u32 i = pipelineCount; i < MaxCullingPipelines; ++i)
			{
				data->drawOffsets[i] = U32_MAX;
				data->maxDraws[i] = 0;
			}
			if (totalDraws == 0) return;

			data->pipelineCount = pipelineCount;

			u64 requiredSize = static_cast<u64>(totalDraws) * sizeof(DrawIndexedIndirectArguments);
			if (requiredSize > drawsCapacity)
			{
				drawsCapacity = Math::Max(requiredSize * 2, static_cast<u64>(sizeof(DrawIndexedIndirectArguments)) * InitialInstanceNumber);
				++drawsGeneration;
				drawsName = String("OpaqueIndirectDraws_") + ToString(drawsGeneration);
			}

			renderGraph.Create(drawsName, RenderGraphBufferDesc{
				.size = drawsCapacity,
				.usage = ResourceUsage::IndirectBuffer,
				.hostVisible = false,
				.perFrame = true
			});

			renderGraph.Create(OpaqueCountsName, RenderGraphBufferDesc{
				.size = sizeof(u32) * MaxCullingPipelines,
				.usage = ResourceUsage::IndirectBuffer,
				.hostVisible = false,
				.perFrame = true
			});

			renderGraph.Create(OpaqueOffsetsName, RenderGraphBufferDesc{
				.size = sizeof(u32) * MaxCullingPipelines,
				.hostVisible = true,
				.persistentMapped = true,
				.perFrame = true
			});

			renderGraph
				.AddPass("CullingPrepare")
				.Stage(RenderStage::Culling)
				.Write(OpaqueCountsName)
				.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					RenderGraph& rg = *pass.GetGraph();

					SceneCullingData* data = rg.GetInstanceData<SceneCullingData>(SceneCullingDataName);
					GPUBuffer*           draws = rg.GetBuffer(drawsName);
					GPUBuffer*           counts = rg.GetBuffer(OpaqueCountsName);
					GPUBuffer*           offsets = rg.GetBuffer(OpaqueOffsetsName);
					if (data == nullptr || draws == nullptr || counts == nullptr || offsets == nullptr) return;

					memcpy(offsets->GetMappedData(), data->drawOffsets, sizeof(u32) * MaxCullingPipelines);
					cmd->FillBuffer(counts, 0, sizeof(u32) * MaxCullingPipelines, 0);

					data->indirectDrawBuffer = draws;
					data->countBuffer = counts;
				});

			renderGraph
				.AddComputePass("Culling", "Skore://Shaders/CullingPass.comp")
				.Stage(RenderStage::Culling)
				.Write(drawsName)
				.WriteRead(OpaqueCountsName)
				.Read(OpaqueOffsetsName)
				.DescriptorSet(1, sceneSet)
				.DescriptorSet(2, globalSet)
				.Constants<i32>([](RenderGraph&, i32& forcedLod)
				{
					forcedLod = RenderDebug::ForcedLod();
				})
				.Dispatch(objects->instanceDataCount, 1, 1);

			//shader-less pass: its Read dependencies transition the culling output to ShaderReadOnly,
			//whose buffer barriers include indirect-command access for the forward pass's indirect draws
			renderGraph
				.AddComputePass("CullingToIndirect", RID{})
				.Stage(RenderStage::Culling)
				.Read(drawsName)
				.Read(OpaqueCountsName);
		}
	};

	void RegisterCullingPass()
	{
		Reflection::Type<CullingPass>();
	}
}
