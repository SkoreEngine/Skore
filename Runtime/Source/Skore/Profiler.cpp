#include "Skore/Profiler.hpp"

#include <chrono>
#include <cstring>

#include "Skore/Graphics/Graphics.hpp"

#ifdef SK_ENABLE_TRACY
#include "tracy/Tracy.hpp"
#endif

namespace Skore
{
	namespace
	{
		constexpr u32 MaxSamples   = 256;
		constexpr u32 BufferCount  = 3;

		using Clock     = std::chrono::high_resolution_clock;
		using TimePoint = Clock::time_point;

		struct Sample
		{
			char              name[64]{};
			f64               cpuStart = 0.0;
			f64               cpuEnd = 0.0;
			u32               gpuQueryStart = 0;
			u32               gpuQueryEnd = 0;
			bool              hasGPU = false;
			i32               depth = 0;
		};

		struct Buffer
		{
			Sample            samples[MaxSamples]{};
			u32               sampleCount = 0;
			GPUQueryPool*     queryPool = nullptr;
			u32               queryIndex = 0;
			bool              queryPoolReset = false;
		};

		// Independent state for a single profiler (CPU or GPU). Each context owns
		// its own triple-buffered samples, nesting stack, built task list and the
		// rolling frame statistics derived from it.
		struct ProfilerContext
		{
			Buffer              buffers[BufferCount]{};
			i32                 writeIdx = 0;
			i32                 readIdx  = 1;

			// Stack for tracking nesting
			i32                 sampleStack[MaxSamples]{};
			i32                 stackDepth = 0;

			// Output tasks for the read buffer
			Profiler::TaskEntry tasks[MaxSamples]{};
			u32                 taskCount = 0;

			// Rolling frame stats
			f64                 frameTimeCur = 0.0;
			f64                 frameTimeMin = 0.0;
			f64                 frameTimeMax = 0.0;
			f64                 frameTimeAvg = 0.0;
			u32                 frameTimeCount = 0;
		};

		// Color palette for auto-coloring based on name hash
		const u32 colorPalette[] = {
			0xFF9C1ABC, // turqoise
			0xFF60CC2E, // emerald
			0xFFDB9834, // peterRiver
			0xFFB6599B, // amethyst
			0xFF0FC4F1, // sunFlower
			0xFF229CE6, // carrot
			0xFF3C4CE7, // alizarin
			0xFF85A016, // greenSea
			0xFF60AE27, // nephritis
			0xFFB98029, // belizeHole
			0xFFAD448E, // wisteria
			0xFF129CF3, // orange
			0xFF0054D3, // pumpkin
			0xFFF0ECE1, // clouds
			0xFFC7C3BD, // silver
			0xFF2B39C0, // pomegranate
		};
		constexpr u32 colorPaletteSize = sizeof(colorPalette) / sizeof(colorPalette[0]);

		ProfilerContext      cpuCtx;
		ProfilerContext      gpuCtx;

		u32                  frameNumber = 0;
		TimePoint            frameStart{};
		bool                 active = false;
		bool                 hasLastFrame = false;

		f32                  timestampPeriod = 0.0f;

		u32 HashName(const char* name)
		{
			u32 hash = 2166136261u;
			while (*name)
			{
				hash ^= static_cast<u32>(*name++);
				hash *= 16777619u;
			}
			return hash;
		}

		u32 GetColorForName(const char* name)
		{
			return colorPalette[HashName(name) % colorPaletteSize];
		}

		f64 GetElapsedSeconds()
		{
			return std::chrono::duration<f64>(Clock::now() - frameStart).count();
		}

		i32 FindTask(ProfilerContext& ctx, const char* name)
		{
			for (u32 i = 0; i < ctx.taskCount; i++)
			{
				if (std::strcmp(ctx.tasks[i].name, name) == 0) return static_cast<i32>(i);
			}
			return -1;
		}

		void AccumulateFrameStat(ProfilerContext& ctx, f64 value)
		{
			if (ctx.frameTimeCount == 0)
			{
				ctx.frameTimeMin = value;
				ctx.frameTimeMax = value;
				ctx.frameTimeAvg = value;
			}
			else
			{
				if (value < ctx.frameTimeMin) ctx.frameTimeMin = value;
				if (value > ctx.frameTimeMax) ctx.frameTimeMax = value;
				ctx.frameTimeAvg += (value - ctx.frameTimeAvg) / f64(ctx.frameTimeCount + 1);
			}
			ctx.frameTimeCur = value;
			ctx.frameTimeCount++;
		}

		void ResetFrameStats(ProfilerContext& ctx)
		{
			ctx.frameTimeCur = 0.0;
			ctx.frameTimeMin = 0.0;
			ctx.frameTimeMax = 0.0;
			ctx.frameTimeAvg = 0.0;
			ctx.frameTimeCount = 0;
		}

		void BeginSample(ProfilerContext& ctx, StringView name, GPUCommandBuffer* cmd)
		{
			if (!active) return;

#ifdef SK_ENABLE_TRACY
			TracyMessageL(name.Data());
#endif

			auto& buf = ctx.buffers[ctx.writeIdx];
			if (buf.sampleCount >= MaxSamples) return;

			u32 idx = buf.sampleCount++;
			auto& sample = buf.samples[idx];

			strncpy(sample.name, name.Data(), sizeof(sample.name) - 1);
			sample.name[std::min<usize>(name.Size(), sizeof(sample.name) - 1)] = '\0';
			sample.cpuStart = GetElapsedSeconds();
			sample.depth = ctx.stackDepth;

			// GPU timestamp
			if (cmd && buf.queryPool)
			{
				if (!buf.queryPoolReset)
				{
					cmd->ResetQueryPool(buf.queryPool, 0, MaxSamples * 2);
					buf.queryPoolReset = true;
				}
				sample.gpuQueryStart = buf.queryIndex++;
				cmd->WriteTimestamp(buf.queryPool, sample.gpuQueryStart);
				sample.hasGPU = true;
			}
			else
			{
				sample.hasGPU = false;
			}

			ctx.sampleStack[ctx.stackDepth++] = idx;
		}

		void EndSample(ProfilerContext& ctx, GPUCommandBuffer* cmd)
		{
			if (!active) return;
			if (ctx.stackDepth <= 0) return;

			ctx.stackDepth--;
			i32 idx = ctx.sampleStack[ctx.stackDepth];

			auto& buf = ctx.buffers[ctx.writeIdx];
			auto& sample = buf.samples[idx];

			sample.cpuEnd = GetElapsedSeconds();

			// GPU timestamp
			if (cmd && sample.hasGPU && buf.queryPool)
			{
				sample.gpuQueryEnd = buf.queryIndex++;
				cmd->WriteTimestamp(buf.queryPool, sample.gpuQueryEnd);
			}
		}

		void BuildTasks(ProfilerContext& ctx, bool gpu)
		{
			auto& buf = ctx.buffers[ctx.readIdx];

			u64 gpuTimestamps[MaxSamples * 2]{};
			bool hasGPUResults = false;
			if (buf.queryIndex > 0 && buf.queryPool)
			{
				hasGPUResults = buf.queryPool->GetResults(0, buf.queryIndex, gpuTimestamps, sizeof(u64), true);
			}

			for (u32 i = 0; i < ctx.taskCount; i++)
			{
				ctx.tasks[i].present = false;
				ctx.tasks[i].cpuTime = 0.0;
				ctx.tasks[i].gpuTime = 0.0;
				ctx.tasks[i].hasGPU = false;
			}

			i32 pathStack[MaxSamples]{};

			for (u32 i = 0; i < buf.sampleCount; i++)
			{
				auto& sample = buf.samples[i];

				i32 idx = FindTask(ctx, sample.name);
				if (idx < 0)
				{
					if (ctx.taskCount >= MaxSamples) continue;

					i32 insertAt = static_cast<i32>(ctx.taskCount);
					if (sample.depth > 0)
					{
						insertAt = pathStack[sample.depth - 1] + 1;
						while (insertAt < static_cast<i32>(ctx.taskCount) && ctx.tasks[insertAt].depth > sample.depth)
						{
							insertAt++;
						}
					}

					for (i32 j = static_cast<i32>(ctx.taskCount); j > insertAt; j--)
					{
						ctx.tasks[j] = ctx.tasks[j - 1];
					}
					ctx.taskCount++;

					idx = insertAt;
					auto& created = ctx.tasks[idx];
					strncpy(created.name, sample.name, sizeof(created.name) - 1);
					created.name[sizeof(created.name) - 1] = '\0';
					created.color = GetColorForName(sample.name);
					created.depth = sample.depth;
					created.cpuTime = 0.0;
					created.gpuTime = 0.0;
					created.cpuMin = 0.0;
					created.cpuMax = 0.0;
					created.cpuAvg = 0.0;
					created.gpuMin = 0.0;
					created.gpuMax = 0.0;
					created.gpuAvg = 0.0;
					created.cpuCount = 0;
					created.gpuCount = 0;
					created.hasGPU = false;
					created.present = false;
				}

				auto& task = ctx.tasks[idx];
				task.present = true;
				task.cpuTime += sample.cpuEnd - sample.cpuStart;

				if (sample.hasGPU && hasGPUResults)
				{
					u64 startTick = gpuTimestamps[sample.gpuQueryStart];
					u64 endTick = gpuTimestamps[sample.gpuQueryEnd];
					task.gpuTime += f64(endTick - startTick) * f64(timestampPeriod) * 1e-9;
					task.hasGPU = true;
				}

				pathStack[sample.depth] = idx;
			}

			f64 frameTotal = 0.0;

			for (u32 i = 0; i < ctx.taskCount; i++)
			{
				auto& task = ctx.tasks[i];
				if (!task.present) continue;

				if (task.cpuCount == 0)
				{
					task.cpuMin = task.cpuTime;
					task.cpuMax = task.cpuTime;
					task.cpuAvg = task.cpuTime;
				}
				else
				{
					if (task.cpuTime < task.cpuMin) task.cpuMin = task.cpuTime;
					if (task.cpuTime > task.cpuMax) task.cpuMax = task.cpuTime;
					task.cpuAvg += (task.cpuTime - task.cpuAvg) / f64(task.cpuCount + 1);
				}
				task.cpuCount++;

				if (task.hasGPU)
				{
					if (task.gpuCount == 0)
					{
						task.gpuMin = task.gpuTime;
						task.gpuMax = task.gpuTime;
						task.gpuAvg = task.gpuTime;
					}
					else
					{
						if (task.gpuTime < task.gpuMin) task.gpuMin = task.gpuTime;
						if (task.gpuTime > task.gpuMax) task.gpuMax = task.gpuTime;
						task.gpuAvg += (task.gpuTime - task.gpuAvg) / f64(task.gpuCount + 1);
					}
					task.gpuCount++;
				}

				// Accumulate the per-frame total from the root level entries only.
				if (task.depth == 0)
				{
					frameTotal += gpu ? task.gpuTime : task.cpuTime;
				}
			}

			// The CPU profiler reports the wall-clock frame time (updated in
			// BeginFrame); only the GPU profiler derives its frame stat here.
			if (gpu)
			{
				AccumulateFrameStat(ctx, frameTotal);
			}
		}

		void AdvanceContext(ProfilerContext& ctx, bool gpu)
		{
			// Advance write index
			ctx.writeIdx = frameNumber % BufferCount;
			// Read from 2 frames ago (ensures GPU work is complete)
			ctx.readIdx = (frameNumber + BufferCount - 2) % BufferCount;

			// Build tasks from the read buffer before clearing write buffer
			if (frameNumber >= 2)
			{
				BuildTasks(ctx, gpu);
			}

			// Clear the write buffer for this frame
			ctx.buffers[ctx.writeIdx].sampleCount = 0;
			ctx.buffers[ctx.writeIdx].queryIndex = 0;
			ctx.buffers[ctx.writeIdx].queryPoolReset = false;

			ctx.stackDepth = 0;
		}
	}

	Profiler::ScopedCpuSample::ScopedCpuSample(const StringView& name) : name(name)
	{
		BeginCpuSample(name);
	}

	Profiler::ScopedCpuSample::~ScopedCpuSample()
	{
		EndCpuSample(name);
	}

	Profiler::ScopedGpuSample::ScopedGpuSample(const StringView& name, GPUCommandBuffer* cmd) : name(name), cmd(cmd)
	{
		BeginGpuSample(name, cmd);
	}

	Profiler::ScopedGpuSample::~ScopedGpuSample()
	{
		EndGpuSample(name, cmd);
	}

	void Profiler::Init()
	{
		GPUDevice* device = Graphics::GetDevice();
		if (!device) return;

		// Get timestamp period from device properties
		// Vulkan provides this in nanoseconds per tick
		auto& props = device->GetProperties();
		timestampPeriod = props.limits.timestampPeriod;

		// Only the GPU profiler needs query pools for timestamps.
		for (u32 i = 0; i < BufferCount; i++)
		{
			QueryPoolDesc desc{};
			desc.type = QueryType::Timestamp;
			desc.queryCount = MaxSamples * 2;
			desc.debugName = "ProfilerQueryPool";
			gpuCtx.buffers[i].queryPool = device->CreateQueryPool(desc);
		}
	}

	void Profiler::Shutdown()
	{
		for (u32 i = 0; i < BufferCount; i++)
		{
			if (gpuCtx.buffers[i].queryPool)
			{
				gpuCtx.buffers[i].queryPool->Destroy();
				gpuCtx.buffers[i].queryPool = nullptr;
			}
		}
	}

	void Profiler::BeginCpuSample(StringView name)
	{
		BeginSample(cpuCtx, name, nullptr);
	}

	void Profiler::EndCpuSample(StringView name)
	{
		EndSample(cpuCtx, nullptr);
	}

	void Profiler::BeginGpuSample(StringView name, GPUCommandBuffer* cmd)
	{
		BeginSample(gpuCtx, name, cmd);
	}

	void Profiler::EndGpuSample(StringView name, GPUCommandBuffer* cmd)
	{
		EndSample(gpuCtx, cmd);
	}

	void Profiler::BeginFrame()
	{
		if (!active) return;

#ifdef SK_ENABLE_TRACY
		FrameMark;
#endif

		AdvanceContext(cpuCtx, false);
		AdvanceContext(gpuCtx, true);

		TimePoint now = Clock::now();
		if (hasLastFrame)
		{
			// Wall-clock frame time is the CPU profiler's frame stat.
			AccumulateFrameStat(cpuCtx, std::chrono::duration<f64>(now - frameStart).count());
		}
		hasLastFrame = true;

		frameStart = now;

		frameNumber++;
	}

	void Profiler::EndFrame()
	{
	}

	const Profiler::TaskEntry* Profiler::GetCpuTasks(u32& count)
	{
		count = cpuCtx.taskCount;
		return cpuCtx.tasks;
	}

	const Profiler::TaskEntry* Profiler::GetGpuTasks(u32& count)
	{
		count = gpuCtx.taskCount;
		return gpuCtx.tasks;
	}

	Profiler::FrameStats Profiler::GetCpuFrameStats()
	{
		return FrameStats{cpuCtx.frameTimeCur, cpuCtx.frameTimeMin, cpuCtx.frameTimeMax, cpuCtx.frameTimeAvg, cpuCtx.frameTimeCount};
	}

	Profiler::FrameStats Profiler::GetGpuFrameStats()
	{
		return FrameStats{gpuCtx.frameTimeCur, gpuCtx.frameTimeMin, gpuCtx.frameTimeMax, gpuCtx.frameTimeAvg, gpuCtx.frameTimeCount};
	}

	void Profiler::ResetStats()
	{
		ProfilerContext* contexts[] = {&cpuCtx, &gpuCtx};
		for (ProfilerContext* ctx : contexts)
		{
			for (u32 i = 0; i < ctx->taskCount; i++)
			{
				ctx->tasks[i].cpuMin = 0.0;
				ctx->tasks[i].cpuMax = 0.0;
				ctx->tasks[i].cpuAvg = 0.0;
				ctx->tasks[i].gpuMin = 0.0;
				ctx->tasks[i].gpuMax = 0.0;
				ctx->tasks[i].gpuAvg = 0.0;
				ctx->tasks[i].cpuCount = 0;
				ctx->tasks[i].gpuCount = 0;
			}
			ResetFrameStats(*ctx);
		}
	}

	void Profiler::SetActive(bool value)
	{
		active = value;
		if (!active)
		{
			ProfilerContext* contexts[] = {&cpuCtx, &gpuCtx};
			for (ProfilerContext* ctx : contexts)
			{
				ctx->taskCount = 0;
				ResetFrameStats(*ctx);
			}
			hasLastFrame = false;
		}
	}

	bool Profiler::IsActive()
	{
		return active;
	}
}
