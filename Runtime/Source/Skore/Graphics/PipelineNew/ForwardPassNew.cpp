#include "PipelineCommonNew.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipelineNew.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"

namespace Skore
{
	//Renders opaque geometry first, then transparent geometry on top, into the same color/depth targets.
	//Transparent draws follow bucket order; per-drawcall back-to-front sorting is not done yet.
	struct ForwardPassNew : DefaultPipelinePassNew
	{
		SK_CLASS(ForwardPassNew, DefaultPipelinePassNew);

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  useInstanceData;
			u32  boneBufferIndex;
		};

		struct PipelineBucket
		{
			Array<GPUPipeline*> pipelines;
			Array<RID>          variants;

			void Cleanup()
			{
				for (GPUPipeline* pipeline : pipelines)
				{
					if (pipeline) pipeline->Destroy();
				}
				pipelines.Clear();
				variants.Clear();
			}
		};

		PipelineBucket opaqueBucket;
		PipelineBucket transparentBucket;
		Scene*         cachedScene = nullptr;

		GPUPipeline*   skyPipeline = nullptr;
		GPURenderPass* skyRenderPass = nullptr;

		~ForwardPassNew() override
		{
			opaqueBucket.Cleanup();
			transparentBucket.Cleanup();
			if (skyPipeline) skyPipeline->Destroy();
		}

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			renderGraph.Create(ForwardColorName, RenderGraphTextureDesc{
				.format = Format::RGBA16_FLOAT,
				.extent = Extent{0, 0},
				.usage = ResourceUsage::ShaderResource,
				.clearColor = Vec4(0.10f, 0.11f, 0.13f, 1.0f)
			});

			renderGraph.Create(ForwardDepthName, RenderGraphTextureDesc{
				.format = Format::D32_FLOAT,
				.extent = Extent{0, 0}
			});

			renderGraph.SetDepthOutput(ForwardDepthName);

			renderGraph
				.AddGraphicsPass("Forward")
				.Stage(RenderStage::Forward)
				.Read(LightInstanceDataNewName)
				.Read(ShadowMapTextureName)
				.Write(ForwardColorName)
				.Write(ForwardDepthName)
				.InvertViewport(true)
				.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					if (scene == nullptr || pass.GetRenderPass() == nullptr) return;

					RenderGraph& rg = *pass.GetGraph();

					GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
					GPUDescriptorSet* sceneSet = rg.GetSceneDescriptorSet();
					if (globalSet == nullptr || sceneSet == nullptr) return;

					LightInstanceDataNew* lightData = rg.GetInstanceData<LightInstanceDataNew>(LightInstanceDataNewName);
					if (lightData == nullptr || !lightData->sceneBindingsReady) return;

					RenderSceneObjects* objects = &scene->renderObjects;

					if (cachedScene != scene)
					{
						opaqueBucket.Cleanup();
						transparentBucket.Cleanup();
						cachedScene = scene;
					}

					GPURenderPass*    renderPass = pass.GetRenderPass();
					GPUDescriptorSet* pipelineSceneSet = rg.GetSceneDescriptorSet(0);
					GPUDescriptorSet* skinningSet = objects->GetSkinningDescriptorSet();

					EnsurePipelines(opaqueBucket, objects->opaquePipelines, renderPass, pipelineSceneSet, globalSet, skinningSet, false);
					EnsurePipelines(transparentBucket, objects->transparentPipelines, renderPass, pipelineSceneSet, globalSet, skinningSet, true);

					cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

					SceneCullingDataNew* cullingData = rg.GetInstanceData<SceneCullingDataNew>(SceneCullingDataNewName);

					RenderBucket(cmd, rg, opaqueBucket, objects->opaquePipelines, sceneSet, globalSet, skinningSet, cullingData);
					RenderSky(cmd, rg, scene, renderPass);
					RenderBucket(cmd, rg, transparentBucket, objects->transparentPipelines, sceneSet, globalSet, skinningSet, nullptr);
				});
		}

		void EnsurePipelines(PipelineBucket& bucket, Array<DrawPipeline>& drawPipelines, GPURenderPass* renderPass, GPUDescriptorSet* sceneSet, GPUDescriptorSet* globalSet, GPUDescriptorSet* skinningSet, bool blendEnable)
		{
			RID forwardShader = Resources::FindByPath("Skore://ShadersNew/DefaultForward.shader");
			if (!forwardShader) return;

			u32 count = static_cast<u32>(drawPipelines.Size());
			if (bucket.pipelines.Size() < count)
			{
				bucket.pipelines.Resize(count, nullptr);
				bucket.variants.Resize(count);
			}

			for (u32 i = 0; i < count; ++i)
			{
				const DrawPipelineDesc& desc = drawPipelines[i].desc;

				RID shader = desc.shader && ShaderResource::IsMaterialShader(desc.shader) ? desc.shader : forwardShader;

				RID material = {};
				RID variant = {};
				if (desc.materialGraph)
				{
					variant = RenderResourceCache::EnsureMaterialVariant(shader, desc.materialGraph, "Default");
					if (variant) material = desc.materialGraph;
				}
				if (!variant)
				{
					variant = ShaderResource::GetVariant(shader, "Default");
				}
				if (!variant && shader != forwardShader)
				{
					shader = forwardShader;
					variant = ShaderResource::GetVariant(forwardShader, "Default");
				}

				if (bucket.pipelines[i] && bucket.variants[i] == variant)
				{
					continue;
				}

				if (bucket.pipelines[i]) bucket.pipelines[i]->Destroy();

				GraphicsPipelineDesc gpuDesc;
				gpuDesc.shader = shader;
				gpuDesc.variant = "Default";
				gpuDesc.material = material;
				gpuDesc.rasterizerState.cullMode = desc.cullMode;
				gpuDesc.depthStencilState.depthTestEnable = true;
				gpuDesc.depthStencilState.depthWriteEnable = desc.depthWrite;
				gpuDesc.depthStencilState.depthCompareOp = desc.depthTest;
				gpuDesc.blendStates = {BlendStateDesc{.blendEnable = blendEnable}};
				gpuDesc.renderPass = renderPass;
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 1, .descriptorSet = sceneSet});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 2, .descriptorSet = globalSet});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 3, .descriptorSet = skinningSet});

				bucket.pipelines[i] = Graphics::CreateGraphicsPipeline(gpuDesc);
				bucket.variants[i] = variant;
			}
		}

		void EnsureSkyPipeline(GPURenderPass* renderPass)
		{
			if (skyPipeline && skyRenderPass == renderPass) return;
			if (skyPipeline) skyPipeline->Destroy();
			skyRenderPass = renderPass;

			DepthStencilStateDesc depthStencilState;
			depthStencilState.depthTestEnable = true;
			depthStencilState.depthWriteEnable = false;
			depthStencilState.depthCompareOp = CompareOp::GreaterEqual; // reverse-Z: skybox sits at far (z=0)

			skyPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/SkyboxRender.raster"),
				.rasterizerState = {
					.cullMode = CullMode::Front
				},
				.depthStencilState = depthStencilState,
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = renderPass,
			});
		}

		//The panoramic sky fills background pixels only: it draws after opaque geometry (which has
		//already written depth) and before transparent geometry (which must blend over the sky).
		void RenderSky(GPUCommandBuffer* cmd, RenderGraph& rg, Scene* scene, GPURenderPass* renderPass)
		{
			EnvironmentResourceCache* skyCache = nullptr;
			scene->Iterate<EnvironmentComponent>([&](EnvironmentComponent* env)
			{
				if (skyCache != nullptr || !env->GetUseSkyboxAsBackground()) return;
				EnvironmentResourceCache* cache = env->GetEnvironmentCache();
				if (cache && cache->descriptorSet) skyCache = cache;
			});

			if (skyCache == nullptr) return;

			EnsureSkyPipeline(renderPass);
			if (skyPipeline == nullptr) return;

			Mat4 viewProj = rg.camera.projection * Mat4(Mat34(rg.camera.view));

			cmd->BindPipeline(skyPipeline);
			cmd->PushConstants(skyPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &viewProj);
			cmd->BindDescriptorSet(skyPipeline, 3, skyCache->descriptorSet, {});
			cmd->Draw(36, 1, 0, 0);
		}

		void RenderBucket(GPUCommandBuffer* cmd, RenderGraph& rg, PipelineBucket& bucket, Array<DrawPipeline>& drawPipelines, GPUDescriptorSet* sceneSet, GPUDescriptorSet* globalSet, GPUDescriptorSet* skinningSet, SceneCullingDataNew* cullingData)
		{
			for (u32 i = 0; i < drawPipelines.Size(); ++i)
			{
				GPUPipeline* pipeline = i < bucket.pipelines.Size() ? bucket.pipelines[i] : nullptr;
				if (!pipeline) continue;

				//the pixel shader may not reference push constants (material graphs read via
				//SK_MaterialIndex), so the valid stages come from the reflected ranges
				ShaderStage pushConstantStages = ShaderStage::None;
				for (const PushConstantRange& range : pipeline->GetPipelineDesc().pushConstants)
				{
					pushConstantStages |= range.stages;
				}

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 1, sceneSet);
				cmd->BindDescriptorSet(pipeline, 2, globalSet);
				cmd->BindDescriptorSet(pipeline, 3, skinningSet);

				if (cullingData != nullptr && cullingData->indirectDrawBuffer != nullptr && cullingData->countBuffer != nullptr && i < cullingData->pipelineCount)
				{
					MeshPushConstants pc{};
					pc.world = Mat4(1.0);
					pc.useInstanceData = 1;
					pc.boneBufferIndex = U32_MAX;

					if (pushConstantStages != ShaderStage::None)
					{
						cmd->PushConstants(pipeline, pushConstantStages, 0, sizeof(MeshPushConstants), &pc);
					}
					cmd->DrawIndexedIndirectCount(cullingData->indirectDrawBuffer,
					                              cullingData->drawOffsets[i] * sizeof(DrawIndexedIndirectArguments),
					                              cullingData->countBuffer,
					                              i * sizeof(u32),
					                              cullingData->maxDraws[i],
					                              sizeof(DrawIndexedIndirectArguments));
					continue;
				}

				for (const Drawcall& drawcall : drawPipelines[i].drawcalls)
				{
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) continue;
					if ((drawcall.layerMask & rg.camera.cullingMask) == 0) continue;

					MeshPushConstants pc;
					pc.world = drawcall.transform;
					pc.materialIndex = drawcall.material->materialIndex;
					pc.vertexByteOffset = drawcall.vertexByteOffset;
					pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
					pc.useInstanceData = 0;
					pc.boneBufferIndex = drawcall.boneBufferIndex;

					if (pushConstantStages != ShaderStage::None)
					{
						cmd->PushConstants(pipeline, pushConstantStages, 0, sizeof(MeshPushConstants), &pc);
					}
					cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
				}
			}
		}
	};

	void RegisterForwardPassNew()
	{
		Reflection::Type<ForwardPassNew>();
	}
}
