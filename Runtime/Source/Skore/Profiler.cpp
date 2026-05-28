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

		Buffer               buffers[BufferCount]{};
		i32                  writeIdx = 0;
		i32                  readIdx  = 1;
		u32                  frameNumber = 0;
		TimePoint            frameStart{};
		bool                 active = false;

		f64                  frameTimeCur = 0.0;
		f64                  frameTimeMin = 0.0;
		f64                  frameTimeMax = 0.0;
		f64                  frameTimeAvg = 0.0;
		u32                  frameTimeCount = 0;
		bool                 hasLastFrame = false;

		// Stack for tracking nesting
		i32                  sampleStack[MaxSamples]{};
		i32                  stackDepth = 0;

		// Output tasks for the read buffer
		Profiler::TaskEntry  tasks[MaxSamples]{};
		u32                  taskCount = 0;

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

		i32 FindTask(const char* name)
		{
			for (u32 i = 0; i < taskCount; i++)
			{
				if (std::strcmp(tasks[i].name, name) == 0) return static_cast<i32>(i);
			}
			return -1;
		}

		void BuildTasks()
		{
			auto& buf = buffers[readIdx];

			u64 gpuTimestamps[MaxSamples * 2]{};
			bool hasGPUResults = false;
			if (buf.queryIndex > 0 && buf.queryPool)
			{
				hasGPUResults = buf.queryPool->GetResults(0, buf.queryIndex, gpuTimestamps, sizeof(u64), true);
			}

			for (u32 i = 0; i < taskCount; i++)
			{
				tasks[i].present = false;
				tasks[i].cpuTime = 0.0;
				tasks[i].gpuTime = 0.0;
				tasks[i].hasGPU = false;
			}

			i32 pathStack[MaxSamples]{};

			for (u32 i = 0; i < buf.sampleCount; i++)
			{
				auto& sample = buf.samples[i];

				i32 idx = FindTask(sample.name);
				if (idx < 0)
				{
					if (taskCount >= MaxSamples) continue;

					i32 insertAt = static_cast<i32>(taskCount);
					if (sample.depth > 0)
					{
						insertAt = pathStack[sample.depth - 1] + 1;
						while (insertAt < static_cast<i32>(taskCount) && tasks[insertAt].depth > sample.depth)
						{
							insertAt++;
						}
					}

					for (i32 j = static_cast<i32>(taskCount); j > insertAt; j--)
					{
						tasks[j] = tasks[j - 1];
					}
					taskCount++;

					idx = insertAt;
					auto& created = tasks[idx];
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

				auto& task = tasks[idx];
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

			for (u32 i = 0; i < taskCount; i++)
			{
				auto& task = tasks[i];
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
			}
		}
	}

	Profiler::ScopedSample::ScopedSample(const StringView& name, GPUCommandBuffer* cmd) : name(name), cmd(cmd)
	{
		BeginSample(name, cmd);
	}

	Profiler::ScopedSample::~ScopedSample()
	{
		EndSample(name, cmd);
	}

	void Profiler::Init()
	{
		GPUDevice* device = Graphics::GetDevice();
		if (!device) return;

		// Get timestamp period from device properties
		// Vulkan provides this in nanoseconds per tick
		auto& props = device->GetProperties();
		timestampPeriod = props.limits.timestampPeriod;

		for (u32 i = 0; i < BufferCount; i++)
		{
			QueryPoolDesc desc{};
			desc.type = QueryType::Timestamp;
			desc.queryCount = MaxSamples * 2;
			desc.debugName = "ProfilerQueryPool";
			buffers[i].queryPool = device->CreateQueryPool(desc);
		}
	}

	void Profiler::Shutdown()
	{
		for (u32 i = 0; i < BufferCount; i++)
		{
			if (buffers[i].queryPool)
			{
				buffers[i].queryPool->Destroy();
				buffers[i].queryPool = nullptr;
			}
		}
	}

	void Profiler::BeginSample(StringView name, GPUCommandBuffer* cmd)
	{
		if (!active) return;

#ifdef SK_ENABLE_TRACY
		TracyMessageL(name.Data());
#endif

		auto& buf = buffers[writeIdx];
		if (buf.sampleCount >= MaxSamples) return;

		u32 idx = buf.sampleCount++;
		auto& sample = buf.samples[idx];

		strncpy(sample.name, name.Data(), sizeof(sample.name) - 1);
		sample.name[std::min<usize>(name.Size(), sizeof(sample.name) - 1)] = '\0';
		sample.cpuStart = GetElapsedSeconds();
		sample.depth = stackDepth;

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

		sampleStack[stackDepth++] = idx;
	}

	void Profiler::EndSample(StringView name, GPUCommandBuffer* cmd)
	{
		if (!active) return;
		if (stackDepth <= 0) return;

		stackDepth--;
		i32 idx = sampleStack[stackDepth];

		auto& buf = buffers[writeIdx];
		auto& sample = buf.samples[idx];

		sample.cpuEnd = GetElapsedSeconds();

		// GPU timestamp
		if (cmd && sample.hasGPU && buf.queryPool)
		{
			sample.gpuQueryEnd = buf.queryIndex++;
			cmd->WriteTimestamp(buf.queryPool, sample.gpuQueryEnd);
		}
	}

	void Profiler::BeginFrame()
	{
		if (!active) return;

#ifdef SK_ENABLE_TRACY
		FrameMark;
#endif

		// Advance write index
		writeIdx = frameNumber % BufferCount;
		// Read from 2 frames ago (ensures GPU work is complete)
		readIdx = (frameNumber + BufferCount - 2) % BufferCount;

		// Build tasks from the read buffer before clearing write buffer
		if (frameNumber >= 2)
		{
			BuildTasks();
		}

		// Clear the write buffer for this frame
		buffers[writeIdx].sampleCount = 0;
		buffers[writeIdx].queryIndex = 0;
		buffers[writeIdx].queryPoolReset = false;

		stackDepth = 0;

		TimePoint now = Clock::now();
		if (hasLastFrame)
		{
			frameTimeCur = std::chrono::duration<f64>(now - frameStart).count();

			if (frameTimeCount == 0)
			{
				frameTimeMin = frameTimeCur;
				frameTimeMax = frameTimeCur;
				frameTimeAvg = frameTimeCur;
			}
			else
			{
				if (frameTimeCur < frameTimeMin) frameTimeMin = frameTimeCur;
				if (frameTimeCur > frameTimeMax) frameTimeMax = frameTimeCur;
				frameTimeAvg += (frameTimeCur - frameTimeAvg) / f64(frameTimeCount + 1);
			}
			frameTimeCount++;
		}
		hasLastFrame = true;

		frameStart = now;

		frameNumber++;
	}

	void Profiler::EndFrame()
	{
	}

	const Profiler::TaskEntry* Profiler::GetTasks(u32& count)
	{
		count = taskCount;
		return tasks;
	}

	Profiler::FrameStats Profiler::GetFrameStats()
	{
		return FrameStats{frameTimeCur, frameTimeMin, frameTimeMax, frameTimeAvg, frameTimeCount};
	}

	void Profiler::ResetStats()
	{
		for (u32 i = 0; i < taskCount; i++)
		{
			tasks[i].cpuMin = 0.0;
			tasks[i].cpuMax = 0.0;
			tasks[i].cpuAvg = 0.0;
			tasks[i].gpuMin = 0.0;
			tasks[i].gpuMax = 0.0;
			tasks[i].gpuAvg = 0.0;
			tasks[i].cpuCount = 0;
			tasks[i].gpuCount = 0;
		}

		frameTimeCur = 0.0;
		frameTimeMin = 0.0;
		frameTimeMax = 0.0;
		frameTimeAvg = 0.0;
		frameTimeCount = 0;
	}

	void Profiler::SetActive(bool value)
	{
		active = value;
		if (!active)
		{
			taskCount = 0;
			frameTimeCur = 0.0;
			frameTimeMin = 0.0;
			frameTimeMax = 0.0;
			frameTimeAvg = 0.0;
			frameTimeCount = 0;
			hasLastFrame = false;
		}
	}

	bool Profiler::IsActive()
	{
		return active;
	}
}