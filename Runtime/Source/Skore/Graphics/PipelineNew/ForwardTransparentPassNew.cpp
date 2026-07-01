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

	//Renders the transparent buckets on top of the opaque pass output. Draw order follows bucket order;
	//per-drawcall back-to-front sorting is not done yet.
	struct ForwardTransparentPassNew : DefaultPipelinePassNew
	{
		SK_CLASS(ForwardTransparentPassNew, DefaultPipelinePassNew);

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

		~ForwardTransparentPassNew() override
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
			RenderGraphPass& pass = renderGraph.AddGraphicsPass("ForwardTransparent");
			RenderGraphPass* passPtr = &pass;

			pass.Stage(RenderStage::Transparent)
			    .WriteRead(ForwardColorName)
			    .WriteRead(ForwardDepthName)
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

			u32 count = static_cast<u32>(objects->transparentPipelines.Size());
			if (pipelines.Size() < count)
			{
				pipelines.Resize(count, nullptr);
				pipelineVariants.Resize(count);
			}

			for (u32 i = 0; i < count; ++i)
			{
				const DrawPipelineDesc& desc = objects->transparentPipelines[i].desc;

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
				gpuDesc.depthStencilState.depthWriteEnable = false;
				gpuDesc.depthStencilState.depthCompareOp = CompareOp::Greater;
				gpuDesc.blendStates = {BlendStateDesc{.blendEnable = true}};
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

			for (u32 i = 0; i < objects->transparentPipelines.Size(); ++i)
			{
				GPUPipeline* pipeline = i < pipelines.Size() ? pipelines[i] : nullptr;
				if (!pipeline) continue;

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 1, sceneSet);
				cmd->BindDescriptorSet(pipeline, 2, globalSet);

				for (const Drawcall& drawcall : objects->transparentPipelines[i].drawcalls)
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

	void RegisterForwardTransparentPassNew()
	{
		Reflection::Type<ForwardTransparentPassNew>();
	}
}
