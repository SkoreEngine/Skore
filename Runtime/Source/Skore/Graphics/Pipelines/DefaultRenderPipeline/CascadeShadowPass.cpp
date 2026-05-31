#include "CascadeShadowPass.hpp"

namespace Skore
{
	// Legacy CPU-side per-drawcall shadow rendering. Kept for reference
	// while the new GPU-driven indirect path bakes in; superseded by
	// CascadeShadowPassBase::Render which builds pipelines + dispatches
	// the CullingShadowPass compute shader + issues DrawIndexedIndirectCount.
	/*
	void CascadeShadowPass::RenderCascade(Scene* scene, GPUCommandBuffer* cmd, const Mat4& viewProj, u32 cascadeIndex)
	{
		RenderSceneObjects* objects = &scene->renderObjects;
		struct ShadowPushConstants
		{
			Mat4 world;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad[2];
		};

		while (shadowMapPipelines.Size() < objects->shadowPipelines.Size())
		{
			const DrawPipelineDesc& desc = objects->shadowPipelines[shadowMapPipelines.Size()].desc;
			RID shadowShader = Resources::FindByPath("Skore://Shaders/ShadowMap.shader");

			Array<String> macros;
			if (desc.hasBones) macros.EmplaceBack("HAS_BONES");

			GPUPipeline* p = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = shadowShader,
				.variant = ShaderResource::GetVariantName(macros),
				.rasterizerState = {
					.cullMode = desc.cullMode,
					.depthClampEnable = true,
				},
				.depthStencilState = {
					.depthTestEnable = true,
					.depthWriteEnable = true,
					.depthCompareOp = CompareOp::LessEqual
				},
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = m_shadowMapRenderPass[0],
				.descriptorSetsOverride = {
					DescriptorSetOverride{
						.set = 0,
						.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
					}
				}
			});
			shadowMapPipelines.EmplaceBack(p);
		}

		cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

		const Frustum& cullFrustum = m_cascadeCullingFrustums[cascadeIndex];

		for (u32 i = 0; i < objects->shadowPipelines.Size(); i++)
		{
			GPUPipeline* pipeline = shadowMapPipelines[i];
			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
			cmd->BindDescriptorSet(pipeline, 2, m_shadowMapDescriptorSets[context->GetCurrentFrame()][cascadeIndex], {});

			for (const Drawcall& drawcall : objects->shadowPipelines[i].drawcalls)
			{
				if (!drawcall.material || drawcall.material->materialIndex == U32_MAX)
				{
					continue;
				}

				if (!IsAABBVisibleInCascade(cullFrustum, drawcall.aabb))
				{
					continue;
				}

				if (drawcall.bones)
				{
					cmd->BindDescriptorSet(pipeline, 3, drawcall.bones);
				}

				ShadowPushConstants pc{};
				pc.world = drawcall.transform;
				pc.vertexByteOffset = drawcall.vertexByteOffset;
				pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
				cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(ShadowPushConstants), &pc);
				cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
			}
		}
	}
	*/
}
