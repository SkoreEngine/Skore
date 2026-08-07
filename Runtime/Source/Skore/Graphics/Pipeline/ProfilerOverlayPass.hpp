#pragma once

#include "Skore/Common.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	struct DrawListContext;
	class GPUCommandBuffer;
	class RenderGraph;
	class RenderGraphPass;

	//F5-toggled CPU/GPU profiler panels drawn over the output color.
	//not reflection-registered on purpose: only pipelines that want the overlay
	//(the player) instantiate it, so it never shows up in editor previews.
	struct SK_API ProfilerOverlayPass : DefaultPipelinePass
	{
		SK_CLASS(ProfilerOverlayPass, DefaultPipelinePass);

		~ProfilerOverlayPass() override;

		void BuildRenderGraph(RenderGraph& renderGraph) override;

	private:
		DrawListContext* drawList = nullptr;
		RID              font = {};
		bool             visible = false;
		bool             togglePressed = false;

		f32  DrawPanel(f32 originX, f32 originY, f32 dpi, const char* label, bool gpu,
		               const Profiler::TaskEntry* entries, u32 count, const Profiler::FrameStats& fs);
		void Render(RenderGraphPass& pass, GPUCommandBuffer* cmd);
	};
}
