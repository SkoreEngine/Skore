#pragma once
#include "Common.hpp"
#include "Core/StringView.hpp"
#include "Graphics/Device.hpp"

#define SK_SCOPED_GPU_ZONE(name, cmd) Skore::Profiler::ScopedGpuSample SK_CONCAT(scopedZone_, __LINE__)(name, cmd)
#define SK_SCOPED_CPU_ZONE(name)      Skore::Profiler::ScopedCpuSample SK_CONCAT(scopedZone_, __LINE__)(name)

namespace Skore::Profiler
{
	struct ScopedCpuSample
	{
		ScopedCpuSample(const StringView& name);
		~ScopedCpuSample();

		StringView name;
	};

	struct ScopedGpuSample
	{
		ScopedGpuSample(const StringView& name, GPUCommandBuffer* cmd);
		~ScopedGpuSample();

		StringView        name;
		GPUCommandBuffer* cmd;
	};

	struct TaskEntry
	{
		f64      cpuTime;
		f64      gpuTime;
		f64      cpuMin;
		f64      cpuMax;
		f64      cpuAvg;
		f64      gpuMin;
		f64      gpuMax;
		f64      gpuAvg;
		u32      cpuCount;
		u32      gpuCount;
		u32      color;
		char     name[64];
		i32      depth;
		bool     hasGPU;
		bool     present;
	};

	struct FrameStats
	{
		f64      current;
		f64      min;
		f64      max;
		f64      avg;
		u32      count;
	};

	SK_API void Init();
	SK_API void Shutdown();

	// CPU profiler
	SK_API void BeginCpuSample(StringView name);
	SK_API void EndCpuSample(StringView name);

	// GPU profiler
	SK_API void BeginGpuSample(StringView name, GPUCommandBuffer* cmd);
	SK_API void EndGpuSample(StringView name, GPUCommandBuffer* cmd);

	SK_API void BeginFrame();
	SK_API void EndFrame();

	SK_API const TaskEntry* GetCpuTasks(u32& count);
	SK_API const TaskEntry* GetGpuTasks(u32& count);

	SK_API FrameStats GetCpuFrameStats();
	SK_API FrameStats GetGpuFrameStats();

	SK_API void ResetStats();

	SK_API void SetActive(bool active);
	SK_API bool IsActive();
}
