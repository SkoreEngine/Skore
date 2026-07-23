#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore
{
	class Scene;
	class RenderGraph;
	class GPUCommandBuffer;

	struct RenderStage
	{
		constexpr static i32 Culling = 100;
		constexpr static i32 ShadowPass = 200;
		constexpr static i32 DepthPrePass = 300;
		constexpr static i32 GBuffer = 400;
		constexpr static i32 DepthLinearize = 500;
		constexpr static i32 Lighting = 600;
		constexpr static i32 Forward = 700;
		constexpr static i32 Transparent = 750;
		constexpr static i32 Indirect = 800;
		constexpr static i32 Composite = 900;
		constexpr static i32 PostProcess = 1000;
		constexpr static i32 Tools = 1100;
		constexpr static i32 UI = 1200;
		constexpr static i32 Swapchain = 1300;
	};

	struct SK_API RenderPipeline : Object
	{
		SK_CLASS(RenderPipeline, Object);

		virtual void BuildRenderGraph(RenderGraph& renderGraph) = 0;
	};

	struct SK_API DefaultPipelinePass : Object
	{
		SK_CLASS(DefaultPipelinePass, Object);

		virtual void BuildRenderGraph(RenderGraph& renderGraph) = 0;
	};

	struct SK_API DefaultRenderPipeline : RenderPipeline
	{
		SK_CLASS(DefaultRenderPipeline, RenderPipeline);

		~DefaultRenderPipeline() override;

		void BuildRenderGraph(RenderGraph& renderGraph) override;

	private:
		Array<DefaultPipelinePass*> passes;
		bool                           discovered = false;
	};

	class SK_API RenderPipelineContext : public Object
	{
	public:
		SK_CLASS(RenderPipelineContext, Object);
		SK_NO_COPY_CONSTRUCTOR(RenderPipelineContext);

		explicit RenderPipelineContext(TypeID pipelineTypeId);
		~RenderPipelineContext() override;

		RenderGraph&       GetRenderGraph() const;
		RenderPipeline* GetPipeline() const;

		void Execute(GPUCommandBuffer* cmd, Scene* scene);

		static RenderPipelineContext* Create(TypeID pipelineTypeId);

		//context that receives camera updates from scene Camera components (game/simulation view)
		static RenderPipelineContext* GetMainContext();
		static void                      SetMainContext(RenderPipelineContext* context);

	private:
		RenderGraph*       renderGraph = nullptr;
		RenderPipeline* pipeline = nullptr;
	};
}
