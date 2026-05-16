#pragma once
#include "Common.hpp"
#include "Core/StringView.hpp"
#include "Graphics/Device.hpp"

#define SK_SCOPED_ZONE(name, cmd) Skore::Profiler::ScopedSample SK_CONCAT(scopedZone_, __LINE__)(name, cmd)
#define SK_SCOPED_CPU_ZONE(name)  Skore::Profiler::ScopedSample SK_CONCAT(scopedZone_, __LINE__)(name, nullptr)

namespace Skore::Profiler
{
	struct ScopedSample
	{
		ScopedSample(const StringView& name, GPUCommandBuffer* cmd);
		~ScopedSample();

		StringView        name;
		GPUCommandBuffer* cmd;
	};

	struct TaskEntry
	{
		double   cpuStartTime;
		double   cpuEndTime;
		double   gpuStartTime;
		double   gpuEndTime;
		u32      color;
		char     name[64];
		i32      depth;
		bool     hasGPU;
	};

	SK_API void Init();
	SK_API void Shutdown();

	SK_API void BeginSample(StringView name, GPUCommandBuffer* cmd);
	SK_API void EndSample(StringView name, GPUCommandBuffer* cmd);

	SK_API void BeginFrame();
	SK_API void EndFrame();

	SK_API const TaskEntry* GetTasks(u32& count);

	SK_API void SetActive(bool active);
	SK_API bool IsActive();
}
