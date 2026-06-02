#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	struct DeferredGBufferPass : RenderPipelinePass
	{
		SK_CLASS(DeferredGBufferPass, RenderPipelinePass);

		Array<GPUPipeline*> opaquePipelines = {};
		Scene*              cachedPipelineOwner = nullptr;

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::GBuffer;
			setup.invertViewport = true;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "SceneCullingData", .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferEmissive", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Write});

			return setup;
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : opaquePipelines)
			{
				pipeline->Destroy();
			}
			opaquePipelines.Clear();
		}

		void Init() override
		{
			//TODO
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene) return;
			RenderSceneObjects* objects = &scene->renderObjects;

			u32 frame = context->GetCurrentFrame();

			if (cachedPipelineOwner != nullptr && cachedPipelineOwner != scene)
			{
				CleanupPipelines();
			}

			while (opaquePipelines.Size() < objects->opaquePipelines.Size())
			{
				const DrawPipelineDesc& desc = objects->opaquePipelines[opaquePipelines.Size()].desc;

				//RID deferredGBuffer = desc.shader ? desc.shader : Resources::FindByPath("Skore://Shaders/DeferredGBuffer.shader");
				RID deferredGBuffer = desc.shader ? desc.shader : Resources::FindByPath("Skore://Shaders/DeferredGBufferIndirect.raster");

				Array<String> macros;
				if (desc.hasBones)  macros.EmplaceBack("HAS_BONES");

				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthWriteEnable = true;
				depthStencilState.depthCompareOp = CompareOp::Greater; // reverse-Z

				GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
					.shader = deferredGBuffer,
					.variant = ShaderResource::GetVariantName(macros),
					.rasterizerState = {
						.cullMode = desc.cullMode,
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{},
						BlendStateDesc{},
						BlendStateDesc{},
						BlendStateDesc{}
					},
					.renderPass = renderPass,
				};

				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 0,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 1,
					.descriptorSet = context->GetSceneDescriptorSet(0)
				});

				opaquePipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}

			cachedPipelineOwner = scene;

			SceneCullingData* cullingData = context->GetInstanceData<SceneCullingData>("SceneCullingData");

			cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

			for (u32 i = 0; i < objects->opaquePipelines.Size(); i++)
			{

				GPUPipeline* pipeline = opaquePipelines[i];

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());


				ScenePipelineCullingData& cullingPipelineData = cullingData->pipelines[i];
				cmd->DrawIndexedIndirectCount(cullingPipelineData.indirectDrawBuffer[frame],
				                              0,
				                              cullingData->countBuffer[frame],
				                              cullingPipelineData.countBufferOffset,
				                              objects->opaquePipelines[i].drawcalls.Size(),
				                              sizeof(DrawIndexedIndirectArguments));

/*



				for (const Drawcall& drawcall : objects->opaquePipelines[i].drawcalls)
				{
					if (!drawcall.aabb.IsOnFrustum(context->camera.frustum))
					{
						continue;
					}

					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX)
					{
						continue;
					}

					if ((drawcall.layerMask & context->camera.cullingMask) == 0)
					{
						continue;
					}

					if (drawcall.bones)
					{
						cmd->BindDescriptorSet(pipeline, 3, drawcall.bones);
					}

					MeshPushConstants pc;
					pc.world = drawcall.transform;
					pc.materialIndex = drawcall.material->materialIndex;
					pc.vertexByteOffset = drawcall.vertexByteOffset;
					pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
					pc.pad = 0;

					cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(MeshPushConstants), &pc);
					cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
				}
				*/

			}
		}

		void Destroy() override
		{
			CleanupPipelines();
		}
	};

	void RegisterDeferredGBufferPass()
	{
		Reflection::Type<DeferredGBufferPass>();
	}
}
