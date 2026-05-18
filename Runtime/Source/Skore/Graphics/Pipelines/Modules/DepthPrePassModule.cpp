#include "Skore/Graphics/Pipelines/Modules/DepthPrePassModule.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/Pipelines/PipelineCommon.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	RenderPipelinePassSetup DepthPrePassPass::GetPassSetup()
	{
		RenderPipelinePassSetup setup;
		setup.type = RenderPipelinePassType::Graphics;
		setup.stage = DefaultPipelineRenderStage::DepthPrePass;
		setup.invertViewport = true;
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Write});
		return setup;
	}

	void DepthPrePassPass::Init()
	{
	}

	void DepthPrePassPass::CleanupPipelines()
	{
		for (GPUPipeline* pipeline : depthPipelines)
		{
			pipeline->Destroy();
		}
		depthPipelines.Clear();
	}

	void DepthPrePassPass::Render(Scene* scene, GPUCommandBuffer* cmd)
	{
		if (!scene) return;
		RenderSceneObjects* objects = &scene->renderObjects;
		struct DepthPushConstants
		{
			Mat4 world;
			u32  meshIndex;
			u32  vertexLayoutIndex;
			u32  pad[2];
		};

		if (cachedPipelineOwner != nullptr && cachedPipelineOwner != scene)
		{
			CleanupPipelines();
		}
		cachedPipelineOwner = scene;

		while (depthPipelines.Size() < objects->opaquePipelines.Size())
		{
			const DrawPipelineDesc& desc = objects->opaquePipelines[depthPipelines.Size()].desc;

			RID depthShader = Resources::FindByPath("Skore://Shaders/DepthPrePass.shader");

			Array<String> macros;
			if (desc.hasBones) macros.EmplaceBack("HAS_BONES");

			GPUPipeline* p = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = depthShader,
				.variant = ShaderResource::GetVariantName(macros),
				.rasterizerState = {
					.cullMode = desc.cullMode,
				},
				.depthStencilState = {
					.depthTestEnable = true,
					.depthWriteEnable = true,
					.depthCompareOp = CompareOp::Less
				},
				.blendStates = {},
				.renderPass = renderPass,
				.descriptorSetsOverride = {
					DescriptorSetOverride{
						.set = 2,
						.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
					}
				}
			});

			depthPipelines.EmplaceBack(p);
		}

		for (u32 i = 0; i < objects->opaquePipelines.Size(); i++)
		{
			GPUPipeline* pipeline = depthPipelines[i];

			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, context->GetSceneDescriptorSet());
			cmd->BindDescriptorSet(pipeline, 2, RenderResourceCache::GetGlobalDescriptorSet());

			for (const Drawcall& drawcall : objects->opaquePipelines[i].drawcalls)
			{
				if (!drawcall.aabb.IsOnFrustum(context->camera.frustum))
				{
					continue;
				}

				if ((drawcall.layerMask & context->camera.cullingMask) == 0)
				{
					continue;
				}

				if (drawcall.bones)
				{
					cmd->BindDescriptorSet(pipeline, 1, drawcall.bones);
				}

				cmd->BindIndexBuffer(drawcall.indexBuffer, 0, IndexType::Uint32);

				DepthPushConstants pc{};
				pc.world = drawcall.transform;
				pc.meshIndex = drawcall.meshIndex;
				pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
				cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(DepthPushConstants), &pc);
				cmd->DrawIndexed(drawcall.indexCount, 1, drawcall.firstIndex, 0, 0);
			}
		}
	}

	void DepthPrePassPass::Destroy()
	{
		CleanupPipelines();
	}

	RenderPipelineModuleSetup DepthPrePassModule::GetSetup()
	{
		RenderPipelineModuleSetup setup;
		setup.passes.EmplaceBack(sktypeid(DepthPrePassPass));
		return setup;
	}

	Array<RenderPipelineResource> DepthPrePassModule::GetResources()
	{
		context->SetDepthOutput(OutputDepthName);

		Array<RenderPipelineResource> resources;
		resources.EmplaceBack(RenderPipelineResource{.name = OutputDepthName, .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::D32_FLOAT});
		return resources;
	}

	void RegisterDepthPrePassModule()
	{
		Reflection::Type<DepthPrePassPass>();
		Reflection::Type<DepthPrePassModule>();
	}
}
