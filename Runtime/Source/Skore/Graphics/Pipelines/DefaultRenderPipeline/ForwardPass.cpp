#include "ForwardPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/ParticleEmitter.hpp"

namespace Skore
{
	RenderPipelinePassSetup ForwardPass::GetPassSetup()
	{
		RenderPipelinePassSetup setup;
		setup.type = RenderPipelinePassType::Graphics;
		setup.stage = PipelineRenderStage::Forward;
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::ReadWrite});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::ReadWrite});
		setup.invertViewport = true;
		return setup;
	}

	void ForwardPass::Init()
	{
		lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");
		{
			DepthStencilStateDesc depthStencilState;
			depthStencilState.depthTestEnable = true;
			depthStencilState.depthCompareOp = CompareOp::LessEqual;

			skyboxMaterialPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
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

		{
			particlePipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/ParticleRender.raster"),
				.rasterizerState = {
					.cullMode = CullMode::None
				},
				.depthStencilState = {
					.depthTestEnable = true,
					.depthWriteEnable = false,
					.depthCompareOp = CompareOp::Less
				},
				.blendStates = {
					BlendStateDesc{
						.blendEnable = true,
						.srcColorBlendFactor = BlendFactor::One,
						.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha,
						.colorBlendOp = BlendOp::Add,
						.srcAlphaBlendFactor = BlendFactor::One,
						.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha,
						.alphaBlendOp = BlendOp::Add
					},
				},
				.renderPass = renderPass,
				.descriptorSetsOverride = {
					DescriptorSetOverride{
						.set = 1,
						.descriptorSet = context->GetSceneDescriptorSet(0)
					}
				}
			});
		}
	}

	void ForwardPass::CleanupPipelines()
	{
		for (GPUPipeline* pipeline : transparencyPipelines)
		{
			pipeline->Destroy();
		}
		transparencyPipelines.Clear();
	}

	void ForwardPass::Render(Scene* scene, GPUCommandBuffer* cmd)
	{
		if (!scene) return;
		RenderSceneObjects* objects = &scene->renderObjects;

		u32 frame = context->GetCurrentFrame();

		if (cachedPipelineOwner != scene)
		{
			CleanupPipelines();
		}
		cachedPipelineOwner = scene;

		while (transparencyPipelines.Size() < objects->transparentPipelines.Size())
		{
			const DrawPipelineDesc& desc = objects->transparentPipelines[transparencyPipelines.Size()].desc;

			RID forwardShader = Resources::FindByPath("Skore://Shaders/ForwardMeshRender.shader");

			Array<String> macros;
			if (desc.hasBones)  macros.EmplaceBack("HAS_BONES");

			DepthStencilStateDesc depthStencilState;
			depthStencilState.depthTestEnable = true;
			depthStencilState.depthWriteEnable = true;
			depthStencilState.depthCompareOp = CompareOp::Less;

			GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
				.shader = forwardShader,
				.variant = ShaderResource::GetVariantName(macros),
				.rasterizerState = {
					.cullMode = desc.cullMode,
				},
				.depthStencilState = depthStencilState,
				.blendStates = {
					BlendStateDesc{
						.blendEnable = true,
						.srcColorBlendFactor = BlendFactor::One,
						.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha,
						.colorBlendOp = BlendOp::Add,
						.srcAlphaBlendFactor = BlendFactor::One,
						.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha,
						.alphaBlendOp = BlendOp::Add
					},
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

			transparencyPipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
		}

		cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

		for (u32 i = 0; i < objects->transparentPipelines.Size(); i++)
		{
			GPUPipeline* pipeline = transparencyPipelines[i];

			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
			cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());

			for (const Drawcall& drawcall : objects->transparentPipelines[i].drawcalls)
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
		}

		scene->Iterate<ParticleEmitter>([&](ParticleEmitter* p)
		{
			GPUBuffer*        particleBuffer = p->GetParticleBuffer();
			GPUDescriptorSet* particleDescriptorSet = p->GetParticleDescriptorSet();
			if (!particleBuffer || !particleDescriptorSet) return;

			u32 maxParticles = p->GetMaxParticles();

			cmd->BindPipeline(particlePipeline);
			cmd->BindDescriptorSet(particlePipeline, 1, context->GetSceneDescriptorSet());
			cmd->BindDescriptorSet(particlePipeline, 3, particleDescriptorSet);
			cmd->Draw(maxParticles * 6, 1, 0, 0);
		});

		EnvironmentComponent* skyboxEnv = nullptr;
		scene->Iterate<EnvironmentComponent>([&](EnvironmentComponent* env)
		{
			if (skyboxEnv == nullptr && env->GetMaterialCache() != nullptr && env->GetUseSkyboxAsBackground())
			{
				skyboxEnv = env;
			}
		});

		if (skyboxEnv)
		{
			MaterialResourceCache* skyMaterial = skyboxEnv->GetMaterialCache();

			cmd->BindPipeline(skyboxMaterialPipeline);
			Mat4 viewProj = context->camera.projection * Mat4(Mat34(context->camera.view));

			cmd->PushConstants(skyboxMaterialPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &viewProj);
			cmd->BindDescriptorSet(skyboxMaterialPipeline, 3, skyMaterial->descriptorSet, {});
			cmd->Draw(36, 1, 0, 0);
		}
	}

	void ForwardPass::Destroy()
	{
		skyboxMaterialPipeline->Destroy();
		particlePipeline->Destroy();

		CleanupPipelines();
	}
}
