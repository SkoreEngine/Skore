#include <cstdio>

#include "Skore/App.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/DrawList.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/IO/Input.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		Color ProfilerColor(u32 c, u8 alpha)
		{
			return Color{
				static_cast<u8>((c >> 16) & 0xFF),
				static_cast<u8>((c >> 8) & 0xFF),
				static_cast<u8>(c & 0xFF),
				alpha
			};
		}
	}

	struct ProfilerOverlayPass : RenderPipelinePass
	{
		SK_CLASS(ProfilerOverlayPass, RenderPipelinePass);

		DrawListContext* drawList = nullptr;
		RID              font = {};
		bool             visible = false;
		bool             togglePressed = false;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::UI;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		void Init() override
		{
			drawList = DrawList::CreateContext();
			font = Resources::FindByPath("Skore://Fonts/DejaVuSans.font");
		}

		void Update() override
		{
			bool down = Input::IsKeyDown(Key::F5);
			if (down && !togglePressed)
			{
				visible = !visible;
				Profiler::SetActive(visible);
			}
			togglePressed = down;
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!visible) return;

			u32                        count = 0;
			const Profiler::TaskEntry* entries = Profiler::GetTasks(count);

			Extent extent = context->GetOutputSize();

			const f32 dpi = Platform::GetWindowDPI(Graphics::GetWindow());

			const f32 fontSize = 15.0f * dpi;
			const f32 lineH = 19.0f * dpi;
			const f32 pad = 8.0f * dpi;
			const f32 originX = 12.0f * dpi;
			const f32 originY = 12.0f * dpi;
			const f32 swatch = 9.0f * dpi;
			const f32 indentStep = 12.0f * dpi;
			const f32 textGap = 14.0f * dpi;
			const f32 nameW = 250.0f * dpi;
			const f32 colW = 66.0f * dpi;
			const f32 panelW = pad + nameW + 8.0f * colW + pad;

			const u32 maxRows = 48;
			u32       rows = count < maxRows ? count : maxRows;

			const f32 headerH = lineH * 2.0f + 4.0f * dpi;
			const f32 panelH = headerH + rows * lineH + pad * 2.0f;

			const f32 numStart = originX + pad + nameW;
			const f32 colCpuCur = numStart;
			const f32 colCpuAvg = numStart + colW;
			const f32 colCpuMin = numStart + colW * 2.0f;
			const f32 colCpuMax = numStart + colW * 3.0f;
			const f32 colGpuCur = numStart + colW * 4.0f;
			const f32 colGpuAvg = numStart + colW * 5.0f;
			const f32 colGpuMin = numStart + colW * 6.0f;
			const f32 colGpuMax = numStart + colW * 7.0f;

			DrawList::AddRectFilled(drawList, Vec2(originX, originY), Vec2(originX + panelW, originY + panelH), Color{10, 12, 16, 210});
			DrawList::AddRect(drawList, Vec2(originX, originY), Vec2(originX + panelW, originY + panelH), Color{60, 64, 72, 255}, dpi);

			f32 rowY = originY + pad + headerH;
			for (u32 i = 0; i < rows; ++i)
			{
				const Profiler::TaskEntry& entry = entries[i];

				f32 indent = static_cast<f32>(entry.depth) * indentStep;
				f32 swatchX = originX + pad + indent;
				f32 swatchY = rowY + (fontSize - swatch) * 0.5f;

				DrawList::AddRectFilled(drawList, Vec2(swatchX, swatchY), Vec2(swatchX + swatch, swatchY + swatch), ProfilerColor(entry.color, 255));

				rowY += lineH;
			}

			char buf[256];

			Profiler::FrameStats fs = Profiler::GetFrameStats();
			std::snprintf(buf, sizeof(buf), "Profiler   FPS %.1f   frame %.2f   avg %.2f   min %.2f   max %.2f ms",
				App::GetFPS(), fs.current * 1000.0, fs.avg * 1000.0, fs.min * 1000.0, fs.max * 1000.0);
			DrawList::AddText(drawList, font, Vec2(originX + pad, originY + pad), fontSize, buf, Color{235, 235, 245, 255});

			const Color cpuHdr{150, 170, 200, 255};
			const Color gpuHdr{150, 200, 170, 255};
			const Color subHdr{140, 140, 150, 255};
			f32 hdrY = originY + pad + lineH;
			DrawList::AddText(drawList, font, Vec2(colCpuCur, hdrY), fontSize, "CPU", cpuHdr);
			DrawList::AddText(drawList, font, Vec2(colCpuAvg, hdrY), fontSize, "avg", subHdr);
			DrawList::AddText(drawList, font, Vec2(colCpuMin, hdrY), fontSize, "min", subHdr);
			DrawList::AddText(drawList, font, Vec2(colCpuMax, hdrY), fontSize, "max", subHdr);
			DrawList::AddText(drawList, font, Vec2(colGpuCur, hdrY), fontSize, "GPU", gpuHdr);
			DrawList::AddText(drawList, font, Vec2(colGpuAvg, hdrY), fontSize, "avg", subHdr);
			DrawList::AddText(drawList, font, Vec2(colGpuMin, hdrY), fontSize, "min", subHdr);
			DrawList::AddText(drawList, font, Vec2(colGpuMax, hdrY), fontSize, "max", subHdr);

			rowY = originY + pad + headerH;

			const Color dash{120, 120, 130, 255};
			const Color cpuBright{200, 215, 235, 255};
			const Color cpuDim{150, 168, 190, 255};
			const Color gpuBright{200, 235, 215, 255};
			const Color gpuDim{150, 190, 168, 255};

			auto drawCell = [&](f32 x, bool show, f64 ms, Color col)
			{
				if (show)
				{
					char tmp[32];
					std::snprintf(tmp, sizeof(tmp), "%.2f", ms);
					DrawList::AddText(drawList, font, Vec2(x, rowY), fontSize, tmp, col);
				}
				else
				{
					DrawList::AddText(drawList, font, Vec2(x, rowY), fontSize, "-", dash);
				}
			};

			for (u32 i = 0; i < rows; ++i)
			{
				const Profiler::TaskEntry& entry = entries[i];

				f32 indent = static_cast<f32>(entry.depth) * indentStep;
				f32 textX = originX + pad + indent + textGap;

				DrawList::AddText(drawList, font, Vec2(textX, rowY), fontSize, entry.name, Color{220, 220, 228, 255});

				drawCell(colCpuCur, entry.present, entry.cpuTime * 1000.0, cpuBright);
				drawCell(colCpuAvg, entry.cpuCount > 0, entry.cpuAvg * 1000.0, cpuDim);
				drawCell(colCpuMin, entry.cpuCount > 0, entry.cpuMin * 1000.0, cpuDim);
				drawCell(colCpuMax, entry.cpuCount > 0, entry.cpuMax * 1000.0, cpuDim);

				drawCell(colGpuCur, entry.present && entry.hasGPU, entry.gpuTime * 1000.0, gpuBright);
				drawCell(colGpuAvg, entry.gpuCount > 0, entry.gpuAvg * 1000.0, gpuDim);
				drawCell(colGpuMin, entry.gpuCount > 0, entry.gpuMin * 1000.0, gpuDim);
				drawCell(colGpuMax, entry.gpuCount > 0, entry.gpuMax * 1000.0, gpuDim);

				rowY += lineH;
			}

			DrawList::Flush(drawList, renderPass, extent, cmd);
		}

		void Destroy() override
		{
			if (drawList)
			{
				DrawList::DestroyContext(drawList);
				drawList = nullptr;
			}
		}
	};

	struct ProfilerOverlayPipelineModule : RenderPipelineModule
	{
		SK_CLASS(ProfilerOverlayPipelineModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(ProfilerOverlayPass));
			return setup;
		}
	};

	void RegisterProfilerOverlayModule()
	{
		Reflection::Type<ProfilerOverlayPass>();
		Reflection::Type<ProfilerOverlayPipelineModule>();
	}
}
