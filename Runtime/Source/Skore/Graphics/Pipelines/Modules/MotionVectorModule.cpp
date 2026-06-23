#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	struct CameraMotionVectorPass : RenderPipelinePass
	{
		SK_CLASS(CameraMotionVectorPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "MotionVector", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/ComputeCameraMotion.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			struct PushConstants
			{
				Mat4  projectionNoJitter;
				Mat4  viewInverse;
				Mat4  previousViewProjectionNoJitter;
				IVec2 resolution;
				Vec2  pad;
			};

			PushConstants pc;
			pc.projectionNoJitter = context->camera.projectionNoJitter;
			pc.viewInverse = context->camera.invView;
			pc.previousViewProjectionNoJitter = context->camera.previousViewProjectionNoJitter;
			pc.resolution = IVec2{static_cast<i32>(context->GetOutputSize().width), static_cast<i32>(context->GetOutputSize().height)};
			pc.pad = Vec2(0.0f);

			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture("MotionVector"), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture(LinearDepthMipChainName), 0);
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(PushConstants), &pc);

			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	struct ObjectMotionVectorPass : RenderPipelinePass
	{
		SK_CLASS(ObjectMotionVectorPass, RenderPipelinePass);

		Array<GPUPipeline*> pipelines;
		Scene*              cachedPipelineOwner = nullptr;
		GPUBuffer*          drawDataBuffer[SK_FRAMES_IN_FLIGHT] = {};
		u32                 drawDataCapacity = 0;

		struct DrawData
		{
			Mat4 currentModelViewProjection;
			Mat4 currentModelViewProjectionNoJitter;
			Mat4 previousModelViewProjectionNoJitter;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad;
		};

		struct PushConstants
		{
			u32 drawDataIndex;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.invertViewport = true;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "MotionVector", .access = RenderPipelineTextureAccess::ReadWrite});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : pipelines)
			{
				if (pipeline)
				{
					pipeline->Destroy();
				}
			}
			pipelines.Clear();
		}

		void DestroyDrawDataBuffers()
		{
			for (GPUBuffer*& buffer : drawDataBuffer)
			{
				if (buffer)
				{
					buffer->Destroy();
					buffer = nullptr;
				}
			}
			drawDataCapacity = 0;
		}

		const DrawPipelineDesc& GetVisiblePipelineDesc(const RenderSceneObjects* objects, u32 visiblePipelineIndex) const
		{
			if (visiblePipelineIndex < objects->opaquePipelines.Size())
			{
				return objects->opaquePipelines[visiblePipelineIndex].desc;
			}
			return objects->transparentPipelines[visiblePipelineIndex - objects->opaquePipelines.Size()].desc;
		}

		void EnsurePipelines(RenderSceneObjects* objects)
		{
			while (pipelines.Size() < objects->GetVisiblePipelineCount())
			{
				const DrawPipelineDesc& desc = GetVisiblePipelineDesc(objects, static_cast<u32>(pipelines.Size()));
				if (desc.hasBones)
				{
					pipelines.EmplaceBack(nullptr);
					continue;
				}

				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthWriteEnable = false;
				depthStencilState.depthCompareOp = CompareOp::GreaterEqual; // reverse-Z

				GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/ObjectMotionVector.raster"),
					.rasterizerState = {
						.cullMode = desc.cullMode,
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = renderPass,
					.allowImmediateSet = true,
				};

				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 0,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 1,
					.descriptorSet = context->GetSceneDescriptorSet(0)
				});

				pipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}
		}

		void EnsureDrawDataCapacity(u32 requiredCount)
		{
			if (requiredCount <= drawDataCapacity) return;

			u32 newCapacity = Math::Max(requiredCount * 2, 64u);
			DestroyDrawDataBuffers();

			for (GPUBuffer*& buffer : drawDataBuffer)
			{
				buffer = Graphics::CreateBuffer(BufferDesc{
					.size = sizeof(DrawData) * newCapacity,
					.usage = ResourceUsage::ShaderResource,
					.hostVisible = true,
					.persistentMapped = true,
					.debugName = "ObjectMotionVectorDrawData"
				});
			}

			drawDataCapacity = newCapacity;
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene) return;

			RenderSceneObjects* objects = &scene->renderObjects;
			if (objects->movedRenderables.Empty()) return;

			if (cachedPipelineOwner != scene)
			{
				CleanupPipelines();
				cachedPipelineOwner = scene;
			}

			EnsurePipelines(objects);

			GPUBuffer* meshDataBuffer = RenderResourceCache::GetMeshDataBuffer();
			if (!meshDataBuffer) return;

			u32 requiredDrawDataCount = 0;
			for (const MovedRenderableObject& moved : objects->movedRenderables)
			{
				objects->ForEachVisibleDrawcallRef(moved.object, [&](u32 pipelineIndex, const Drawcall& drawcall)
				{
					if (pipelineIndex >= pipelines.Size() || pipelines[pipelineIndex] == nullptr) return;
					if (drawcall.bones || drawcall.boneBufferIndex != U32_MAX) return;
					if (!drawcall.aabb.IsOnFrustum(context->camera.frustum)) return;
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) return;
					if ((drawcall.layerMask & context->camera.cullingMask) == 0) return;
					++requiredDrawDataCount;
				});
			}
			if (requiredDrawDataCount == 0) return;

			const u32 frame = context->GetCurrentFrame();
			EnsureDrawDataCapacity(requiredDrawDataCount);

			DrawData* drawData = static_cast<DrawData*>(drawDataBuffer[frame]->GetMappedData());
			u32 drawDataIndex = 0;

			cmd->BindIndexBuffer(meshDataBuffer, 0, IndexType::Uint32);

			GPUPipeline* boundPipeline = nullptr;
			for (const MovedRenderableObject& moved : objects->movedRenderables)
			{
				objects->ForEachVisibleDrawcallRef(moved.object, [&](u32 pipelineIndex, const Drawcall& drawcall)
				{
					if (pipelineIndex >= pipelines.Size()) return;

					GPUPipeline* pipeline = pipelines[pipelineIndex];
					if (!pipeline) return;

					if (drawcall.bones || drawcall.boneBufferIndex != U32_MAX) return;

					if (!drawcall.aabb.IsOnFrustum(context->camera.frustum)) return;
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) return;
					if ((drawcall.layerMask & context->camera.cullingMask) == 0) return;

					if (boundPipeline != pipeline)
					{
						boundPipeline = pipeline;
						cmd->BindPipeline(pipeline);
						cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
						cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
						cmd->SetBuffer(pipeline, 2, 0, drawDataBuffer[frame], 0, sizeof(DrawData) * requiredDrawDataCount);
					}

					DrawData& data = drawData[drawDataIndex];
					data.currentModelViewProjection = context->camera.viewProjection * drawcall.transform;
					data.currentModelViewProjectionNoJitter = context->camera.viewProjectionNoJitter * drawcall.transform;
					data.previousModelViewProjectionNoJitter = context->camera.previousViewProjectionNoJitter * moved.previousTransform;
					data.materialIndex = drawcall.material->materialIndex;
					data.vertexByteOffset = drawcall.vertexByteOffset;
					data.vertexLayoutIndex = drawcall.vertexLayoutIndex;
					data.pad = 0;

					PushConstants pc{drawDataIndex++};
					cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(PushConstants), &pc);
					cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
				});
			}
		}

		void Destroy() override
		{
			CleanupPipelines();
			DestroyDrawDataBuffers();
		}
	};

	struct SkinnedMotionVectorPass : RenderPipelinePass
	{
		SK_CLASS(SkinnedMotionVectorPass, RenderPipelinePass);

		Array<GPUPipeline*> pipelines;
		Scene*              cachedPipelineOwner = nullptr;
		GPUBuffer*          drawDataBuffer[SK_FRAMES_IN_FLIGHT] = {};
		u32                 drawDataCapacity = 0;

		struct DrawData
		{
			Mat4 currentModelViewProjection;
			Mat4 currentModelViewProjectionNoJitter;
			Mat4 previousModelViewProjectionNoJitter;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  boneBufferIndex;
			u32  bonesChanged;
			u32  pad[3];
		};

		struct PushConstants
		{
			u32 drawDataIndex;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.invertViewport = true;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "MotionVector", .access = RenderPipelineTextureAccess::ReadWrite});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : pipelines)
			{
				if (pipeline)
				{
					pipeline->Destroy();
				}
			}
			pipelines.Clear();
		}

		void DestroyDrawDataBuffers()
		{
			for (GPUBuffer*& buffer : drawDataBuffer)
			{
				if (buffer)
				{
					buffer->Destroy();
					buffer = nullptr;
				}
			}
			drawDataCapacity = 0;
		}

		const DrawPipelineDesc& GetVisiblePipelineDesc(const RenderSceneObjects* objects, u32 visiblePipelineIndex) const
		{
			if (visiblePipelineIndex < objects->opaquePipelines.Size())
			{
				return objects->opaquePipelines[visiblePipelineIndex].desc;
			}
			return objects->transparentPipelines[visiblePipelineIndex - objects->opaquePipelines.Size()].desc;
		}

		void EnsurePipelines(RenderSceneObjects* objects)
		{
			while (pipelines.Size() < objects->GetVisiblePipelineCount())
			{
				const DrawPipelineDesc& desc = GetVisiblePipelineDesc(objects, static_cast<u32>(pipelines.Size()));
				if (!desc.hasBones)
				{
					pipelines.EmplaceBack(nullptr);
					continue;
				}

				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthWriteEnable = false;
				depthStencilState.depthCompareOp = CompareOp::GreaterEqual; // reverse-Z

				GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/SkinnedMotionVector.raster"),
					.rasterizerState = {
						.cullMode = desc.cullMode,
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = renderPass,
					.allowImmediateSet = true,
				};

				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 0,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 1,
					.descriptorSet = objects->GetSkinningDescriptorSet()
				});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 2,
					.descriptorSet = objects->GetPreviousSkinningDescriptorSet()
				});

				pipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}
		}

		void EnsureDrawDataCapacity(u32 requiredCount)
		{
			if (requiredCount <= drawDataCapacity) return;

			u32 newCapacity = Math::Max(requiredCount * 2, 64u);
			DestroyDrawDataBuffers();

			for (GPUBuffer*& buffer : drawDataBuffer)
			{
				buffer = Graphics::CreateBuffer(BufferDesc{
					.size = sizeof(DrawData) * newCapacity,
					.usage = ResourceUsage::ShaderResource,
					.hostVisible = true,
					.persistentMapped = true,
					.debugName = "SkinnedMotionVectorDrawData"
				});
			}

			drawDataCapacity = newCapacity;
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene) return;

			RenderSceneObjects* objects = &scene->renderObjects;
			if (objects->movedRenderables.Empty()) return;

			if (cachedPipelineOwner != scene)
			{
				CleanupPipelines();
				cachedPipelineOwner = scene;
			}

			EnsurePipelines(objects);

			GPUBuffer* meshDataBuffer = RenderResourceCache::GetMeshDataBuffer();
			if (!meshDataBuffer) return;

			u32 requiredDrawDataCount = 0;
			for (const MovedRenderableObject& moved : objects->movedRenderables)
			{
				objects->ForEachVisibleDrawcallRef(moved.object, [&](u32 pipelineIndex, const Drawcall& drawcall)
				{
					if (pipelineIndex >= pipelines.Size() || pipelines[pipelineIndex] == nullptr) return;
					if (drawcall.boneBufferIndex == U32_MAX) return;
					if (!drawcall.aabb.IsOnFrustum(context->camera.frustum)) return;
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) return;
					if ((drawcall.layerMask & context->camera.cullingMask) == 0) return;
					++requiredDrawDataCount;
				});
			}
			if (requiredDrawDataCount == 0) return;

			const u32 frame = context->GetCurrentFrame();
			EnsureDrawDataCapacity(requiredDrawDataCount);

			DrawData* drawData = static_cast<DrawData*>(drawDataBuffer[frame]->GetMappedData());
			u32 drawDataIndex = 0;

			cmd->BindIndexBuffer(meshDataBuffer, 0, IndexType::Uint32);

			GPUPipeline* boundPipeline = nullptr;
			for (const MovedRenderableObject& moved : objects->movedRenderables)
			{
				const u32 bonesChanged = moved.bonesChanged ? 1u : 0u;
				objects->ForEachVisibleDrawcallRef(moved.object, [&](u32 pipelineIndex, const Drawcall& drawcall)
				{
					if (pipelineIndex >= pipelines.Size()) return;

					GPUPipeline* pipeline = pipelines[pipelineIndex];
					if (!pipeline) return;

					if (drawcall.boneBufferIndex == U32_MAX) return;

					if (!drawcall.aabb.IsOnFrustum(context->camera.frustum)) return;
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) return;
					if ((drawcall.layerMask & context->camera.cullingMask) == 0) return;

					if (boundPipeline != pipeline)
					{
						boundPipeline = pipeline;
						cmd->BindPipeline(pipeline);
						cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
						cmd->BindDescriptorSet(pipeline, 1, objects->GetSkinningDescriptorSet());
						cmd->BindDescriptorSet(pipeline, 2, objects->GetPreviousSkinningDescriptorSet());
						cmd->SetBuffer(pipeline, 3, 0, drawDataBuffer[frame], 0, sizeof(DrawData) * requiredDrawDataCount);
					}

					DrawData& data = drawData[drawDataIndex];
					data.currentModelViewProjection = context->camera.viewProjection * drawcall.transform;
					data.currentModelViewProjectionNoJitter = context->camera.viewProjectionNoJitter * drawcall.transform;
					data.previousModelViewProjectionNoJitter = context->camera.previousViewProjectionNoJitter * moved.previousTransform;
					data.materialIndex = drawcall.material->materialIndex;
					data.vertexByteOffset = drawcall.vertexByteOffset;
					data.vertexLayoutIndex = drawcall.vertexLayoutIndex;
					data.boneBufferIndex = drawcall.boneBufferIndex;
					data.bonesChanged = bonesChanged;
					data.pad[0] = 0;
					data.pad[1] = 0;
					data.pad[2] = 0;

					PushConstants pc{drawDataIndex++};
					cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(PushConstants), &pc);
					cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
				});
			}
		}

		void Destroy() override
		{
			CleanupPipelines();
			DestroyDrawDataBuffers();
		}
	};

	struct MotionVectorModule : RenderPipelineModule
	{
		SK_CLASS(MotionVectorModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(CameraMotionVectorPass));
			setup.passes.EmplaceBack(sktypeid(ObjectMotionVectorPass));
			setup.passes.EmplaceBack(sktypeid(SkinnedMotionVectorPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = "MotionVector", .type = RenderPipelineResourceType::Attachment, .format = Format::RG16_FLOAT, .textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess});
			return resources;
		}

		bool IsEnabled() override
		{
			return context->IsMotionVectorRequired();
		}
	};

	void RegisterMotionVectorModule()
	{
		Reflection::Type<CameraMotionVectorPass>();
		Reflection::Type<ObjectMotionVectorPass>();
		Reflection::Type<SkinnedMotionVectorPass>();
		Reflection::Type<MotionVectorModule>();
	}
}
