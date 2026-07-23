#pragma once

#include "Skore/Graphics/DebugDraw.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	class GPUBuffer;
	class GPUPipeline;
	class GPURenderPass;
	class RenderGraph;
	class RenderGraphPass;
	class SceneViewWindow;

	//default pipeline plus editor-only passes: selection outline, grid, debug physics,
	//navmesh and gizmo drawing. one instance per scene view window.
	struct SceneViewRenderPipeline : DefaultRenderPipeline
	{
		SK_CLASS(SceneViewRenderPipeline, DefaultRenderPipeline);

		SceneViewWindow* window = nullptr;

		~SceneViewRenderPipeline() override;

		void BuildRenderGraph(RenderGraph& renderGraph) override;

	private:
		struct CachedPipeline
		{
			GPUPipeline*   pipeline = nullptr;
			GPURenderPass* renderPass = nullptr;
		};

		CachedPipeline unlitPipeline;
		CachedPipeline outlineDistancePipeline;
		CachedPipeline outlineCompositePipeline;
		CachedPipeline gridPipeline;
		CachedPipeline debugPhysicsPipeline;
		CachedPipeline drawNavMeshPipeline;

		GPUBuffer* navMeshVertexBuffer = nullptr;
		u32        navMeshVertexCount = 0;
		u32        navMeshVersion = 0;

		DebugDraw debugDraw;

		void RenderSelectionMask(RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd);
		void RenderOutlineDistance(RenderGraphPass& pass, GPUCommandBuffer* cmd);
		void RenderTools(RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd);
	};

	void RegisterSceneViewRenderPipeline();
}
