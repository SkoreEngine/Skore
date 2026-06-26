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
		constexpr static i32 Indirect = 800;
		constexpr static i32 Composite = 900;
		constexpr static i32 PostProcess = 1000;
		constexpr static i32 Tools = 1100;
		constexpr static i32 UI = 1200;
		constexpr static i32 Swapchain = 1300;
	};

	struct SK_API RenderPipelineNew : Object
	{
		SK_CLASS(RenderPipelineNew, Object);

		virtual void BuildRenderGraph(RenderGraph& renderGraph) = 0;
	};

	struct SK_API DefaultPipelinePassNew : Object
	{
		SK_CLASS(DefaultPipelinePassNew, Object);

		virtual void BuildRenderGraph(RenderGraph& renderGraph) = 0;
	};

	struct SK_API DefaultRenderPipelineNew : RenderPipelineNew
	{
		SK_CLASS(DefaultRenderPipelineNew, RenderPipelineNew);

		~DefaultRenderPipelineNew() override;

		void BuildRenderGraph(RenderGraph& renderGraph) override;

	private:
		Array<DefaultPipelinePassNew*> passes;
		bool                           discovered = false;
	};

	class SK_API RenderPipelineContextNew : public Object
	{
	public:
		SK_CLASS(RenderPipelineContextNew, Object);
		SK_NO_COPY_CONSTRUCTOR(RenderPipelineContextNew);

		explicit RenderPipelineContextNew(TypeID pipelineTypeId);
		~RenderPipelineContextNew() override;

		RenderGraph& GetRenderGraph() const;

		void Execute(GPUCommandBuffer* cmd, Scene* scene);

		static RenderPipelineContextNew* Create(TypeID pipelineTypeId);

	private:
		RenderGraph*       renderGraph = nullptr;
		RenderPipelineNew* pipeline = nullptr;
	};
}
