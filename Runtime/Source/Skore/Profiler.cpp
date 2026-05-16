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
			double            cpuStart = 0.0;
			double            cpuEnd = 0.0;
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

		double GetElapsedSeconds()
		{
			return std::chrono::duration<double>(Clock::now() - frameStart).count();
		}

		void BuildTasks()
		{
			taskCount = 0;

			auto& buf = buffers[readIdx];

			// Read GPU timestamps if available
			u64 gpuTimestamps[MaxSamples * 2]{};
			bool hasGPUResults = false;
			if (buf.queryIndex > 0 && buf.queryPool)
			{
				hasGPUResults = buf.queryPool->GetResults(0, buf.queryIndex, gpuTimestamps, sizeof(u64), true);
			}

			double gpuBaseTime = -1.0;

			for (u32 i = 0; i < buf.sampleCount; i++)
			{
				auto& sample = buf.samples[i];

				if (taskCount >= MaxSamples) break;

				auto& task = tasks[taskCount++];
				task.cpuStartTime = sample.cpuStart;
				task.cpuEndTime = sample.cpuEnd;
				task.color = GetColorForName(sample.name);
				strncpy(task.name, sample.name, sizeof(task.name) - 1);
				task.name[sizeof(task.name) - 1] = '\0';
				task.depth = sample.depth;
				task.hasGPU = false;
				task.gpuStartTime = 0.0;
				task.gpuEndTime = 0.0;

				// GPU task
				if (sample.hasGPU && hasGPUResults)
				{
					u64 startTick = gpuTimestamps[sample.gpuQueryStart];
					u64 endTick = gpuTimestamps[sample.gpuQueryEnd];

					double startSec = double(startTick) * double(timestampPeriod) * 1e-9;
					double endSec = double(endTick) * double(timestampPeriod) * 1e-9;

					if (gpuBaseTime < 0.0)
						gpuBaseTime = startSec;

					task.gpuStartTime = startSec - gpuBaseTime;
					task.gpuEndTime = endSec - gpuBaseTime;
					task.hasGPU = true;
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
		frameStart = Clock::now();

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

	void Profiler::SetActive(bool value)
	{
		active = value;
		if (!active)
		{
			taskCount = 0;
		}
	}

	bool Profiler::IsActive()
	{
		return active;
	}
}