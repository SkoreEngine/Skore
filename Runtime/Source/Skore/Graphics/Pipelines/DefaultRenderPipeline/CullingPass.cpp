#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	struct CullingPass : RenderPipelinePass
	{
		SK_CLASS(CullingPass, RenderPipelinePass);
		Scene* cachedPipelineOwner = nullptr;

		GPUPipeline* pipeline{};
		GPUDescriptorSet* descriptorSet[SK_FRAMES_IN_FLIGHT]{};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::Culling;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "SceneCullingData", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void CreateDescriptorSet()
		{
			for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; i++)
			{
				descriptorSet[i] = Graphics::CreateDescriptorSet(DescriptorSetDesc{
					.bindings = {
						DescriptorSetLayoutBinding{
							.binding = 0,
							.count = 256,
							.descriptorType = DescriptorType::StorageBuffer,
							.renderType = RenderType::Array
						},
						DescriptorSetLayoutBinding{
							.binding = 1,
							.descriptorType = DescriptorType::StorageBuffer,
						},
					}
				});
			}

		}

		void Init() override
		{
			CreateDescriptorSet();

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/CullingPass.comp"),
				.descriptorSetsOverride = {
					DescriptorSetOverride{
						.set = 0,
						.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
					},
					DescriptorSetOverride{
						.set = 1,
						.descriptorSet = context->GetSceneDescriptorSet(0)
					},
					DescriptorSetOverride{
						.set = 2,
						.descriptorSet = descriptorSet[0]
					}
				}
			});
		}

		void PrepareBuffers(Scene* scene, SceneCullingData* data)
		{
			if (cachedPipelineOwner != nullptr && cachedPipelineOwner != scene)
			{
				for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; i++)
				{
					for (auto& pipelineData : data->pipelines)
					{
						if (pipelineData.indirectDrawBuffer[i])
						{
							pipelineData.indirectDrawBuffer[i]->Destroy();
							pipelineData.indirectDrawBuffer[i] = nullptr;
						}
					}
					data->countBuffer[i]->Destroy();
					data->countBuffer[i] = nullptr;
				}
				data->pipelines.Clear();
			}

			cachedPipelineOwner = scene;

			if (data->pipelines.Size() < scene->renderObjects.opaquePipelines.Size())
			{
				data->pipelines.Resize(scene->renderObjects.opaquePipelines.Size());
			}

			for (int pipelineIndex = 0; pipelineIndex < scene->renderObjects.opaquePipelines.Size(); ++pipelineIndex)
			{

				usize requiredSize = scene->renderObjects.opaquePipelines[pipelineIndex].drawcalls.Size() * sizeof(DrawIndexedIndirectArguments);
				usize currentSize = data->pipelines[pipelineIndex].indirectDrawBuffer[0] ? data->pipelines[pipelineIndex].indirectDrawBuffer[0]->GetDesc().size : 0;

				if (requiredSize >= currentSize)
				{
					for (u32 frame = 0; frame < SK_FRAMES_IN_FLIGHT; frame++)
					{
						data->pipelines[pipelineIndex].indirectDrawBuffer[frame] = Graphics::CreateBuffer(BufferDesc{
							.size = sizeof(DrawIndexedIndirectArguments) * scene->renderObjects.opaquePipelines[pipelineIndex].drawcalls.Size() * 2,
							.usage = ResourceUsage::IndirectBuffer | ResourceUsage::UnorderedAccess,
							.hostVisible = false,
							.persistentMapped = false,
							.debugName = "IndirectDrawBuffer"
						});
					}
				}
				data->pipelines[pipelineIndex].countBufferOffset = sizeof(u32) * pipelineIndex;
			}

			if (scene->renderObjects.opaquePipelines.Size() > 0 && (data->countBuffer[0] == nullptr || data->countBuffer[0]->GetDesc().size < scene->renderObjects.opaquePipelines.Size() * sizeof(u32)))
			{
				for (u32 frame = 0; frame < SK_FRAMES_IN_FLIGHT; frame++)
				{
					if (data->countBuffer[frame])
					{
						data->countBuffer[frame]->Destroy();
					}

					data->countBuffer[frame] = Graphics::CreateBuffer(BufferDesc{
						.size = sizeof(u32) * scene->renderObjects.opaquePipelines.Size(),
						.usage = ResourceUsage::IndirectBuffer | ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest,
						.hostVisible = false,
						.persistentMapped = false,
						.debugName = "IndirectDrawBufferCount"
					});
				}
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (scene->renderObjects.opaquePipelines.Size() > 0)
			{
				SceneCullingData* data = context->GetInstanceData<SceneCullingData>("SceneCullingData");
				PrepareBuffers(scene, data);

				u32 frame = context->GetCurrentFrame();

				cmd->FillBuffer(data->countBuffer[frame], 0, sizeof(u32) * scene->renderObjects.opaquePipelines.Size(), 0);
				//TODO: replace by a proper barrier
				cmd->MemoryBarrier();

				for (int i = 0; i < scene->renderObjects.opaquePipelines.Size(); ++i)
				{
					DescriptorUpdate update = {};
					update.type = DescriptorType::StorageBuffer;
					update.binding = 0;
					update.arrayElement = i;
					update.buffer = data->pipelines[i].indirectDrawBuffer[frame];
					descriptorSet[frame]->Update(update);
				}
				descriptorSet[frame]->UpdateBuffer(1, data->countBuffer[frame], 0, 0);

				cmd->BindPipeline(pipeline);

				cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 2, descriptorSet[context->GetCurrentFrame()]);

				i32 forcedLod = RenderDebug::ForcedLod();
				cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(i32), &forcedLod);

				cmd->Dispatch((scene->renderObjects.instanceDataCount + 15) / 16, 1, 1);

				//cmd->ResourceBarrier()

				//TODO: replace by a proper barrier
				cmd->MemoryBarrier();
			}
		}
	};

	void RegisterCullingPass()
	{
		Reflection::Type<CullingPass>();
	}
}
