#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipelineNew.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	namespace
	{
		constexpr const char* ForwardColorName = "ForwardColor";
		constexpr const char* ForwardDepthName = "ForwardDepth";
	}

	struct ForwardOpaquePassNew : DefaultPipelinePassNew
	{
		SK_CLASS(ForwardOpaquePassNew, DefaultPipelinePassNew);

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad;
		};

		Array<GPUPipeline*> pipelines;
		Array<RID>          pipelineVariants;
		Scene*              cachedScene = nullptr;

		~ForwardOpaquePassNew() override
		{
			CleanupPipelines();
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : pipelines)
			{
				if (pipeline) pipeline->Destroy();
			}
			pipelines.Clear();
			pipelineVariants.Clear();
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

			renderGraph.SetColorOutput(ForwardColorName);
			renderGraph.SetDepthOutput(ForwardDepthName);

			RenderGraphPass& pass = renderGraph.AddGraphicsPass("ForwardOpaque");
			RenderGraphPass* passPtr = &pass;

			pass.Stage(RenderStage::Forward)
			    .Write(ForwardColorName)
			    .Write(ForwardDepthName)
			    .InvertViewport(true)
			    .Render([this, passPtr](RenderGraph& rg, Scene* scene, GPUCommandBuffer* cmd)
			    {
				    Render(rg, scene, cmd, passPtr->GetRenderPass());
			    });
		}

		void EnsurePipelines(RenderSceneObjects* objects, GPURenderPass* renderPass, GPUDescriptorSet* sceneSet, GPUDescriptorSet* globalSet)
		{
			RID forwardShader = Resources::FindByPath("Skore://ShadersNew/ForwardOpaque.raster");
			if (!forwardShader) return;

			u32 count = static_cast<u32>(objects->opaquePipelines.Size());
			if (pipelines.Size() < count)
			{
				pipelines.Resize(count, nullptr);
				pipelineVariants.Resize(count);
			}

			for (u32 i = 0; i < count; ++i)
			{
				const DrawPipelineDesc& desc = objects->opaquePipelines[i].desc;

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

				if (pipelines[i] && pipelineVariants[i] == variant)
				{
					continue;
				}

				if (pipelines[i]) pipelines[i]->Destroy();

				GraphicsPipelineDesc gpuDesc;
				gpuDesc.shader = shader;
				gpuDesc.variant = "Default";
				gpuDesc.material = material;
				gpuDesc.rasterizerState.cullMode = desc.cullMode;
				gpuDesc.depthStencilState.depthTestEnable = true;
				gpuDesc.depthStencilState.depthWriteEnable = desc.depthWrite;
				gpuDesc.depthStencilState.depthCompareOp = desc.depthTest;
				gpuDesc.blendStates = {BlendStateDesc{}};
				gpuDesc.renderPass = renderPass;
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 1, .descriptorSet = sceneSet});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 2, .descriptorSet = globalSet});

				pipelines[i] = Graphics::CreateGraphicsPipeline(gpuDesc);
				pipelineVariants[i] = variant;
			}
		}

		void Render(RenderGraph& rg, Scene* scene, GPUCommandBuffer* cmd, GPURenderPass* renderPass)
		{
			if (scene == nullptr || renderPass == nullptr) return;

			GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
			GPUDescriptorSet* sceneSet  = rg.GetSceneDescriptorSet();
			if (globalSet == nullptr || sceneSet == nullptr) return;

			RenderSceneObjects* objects = &scene->renderObjects;

			if (cachedScene != scene)
			{
				CleanupPipelines();
				cachedScene = scene;
			}

			EnsurePipelines(objects, renderPass, rg.GetSceneDescriptorSet(0), globalSet);

			cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

			for (u32 i = 0; i < objects->opaquePipelines.Size(); ++i)
			{
				GPUPipeline* pipeline = pipelines[i];
				if (!pipeline) continue;

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 1, sceneSet);
				cmd->BindDescriptorSet(pipeline, 2, globalSet);

				for (const Drawcall& drawcall : objects->opaquePipelines[i].drawcalls)
				{
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) continue;
					if ((drawcall.layerMask & rg.camera.cullingMask) == 0) continue;

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
		}
	};

	void RegisterForwardOpaquePassNew()
	{
		Reflection::Type<ForwardOpaquePassNew>();
	}
}
