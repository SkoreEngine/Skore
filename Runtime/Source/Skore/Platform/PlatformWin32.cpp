#include "Skore/Platform/Platform.hpp"

#if defined(SK_WIN)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <psapi.h>

#ifdef GetProcessMemoryInfo
#undef GetProcessMemoryInfo
#endif

#pragma comment(lib, "psapi.lib")

namespace Skore
{
	static u64 FileTimeToU64(const FILETIME& ft)
	{
		return (static_cast<u64>(ft.dwHighDateTime) << 32) | static_cast<u64>(ft.dwLowDateTime);
	}

	ProcessMemoryInfo Platform::GetProcessMemoryInfo()
	{
		ProcessMemoryInfo info{};
		PROCESS_MEMORY_COUNTERS counters{};
		if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		{
			info.workingSetBytes     = counters.WorkingSetSize;
			info.peakWorkingSetBytes = counters.PeakWorkingSetSize;
		}
		return info;
	}

	SystemMemoryInfo Platform::GetSystemMemoryInfo()
	{
		SystemMemoryInfo info{};
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(status);
		if (GlobalMemoryStatusEx(&status))
		{
			info.totalBytes     = status.ullTotalPhys;
			info.availableBytes = status.ullAvailPhys;
		}
		return info;
	}

	f32 Platform::GetProcessCPUUsage()
	{
		static u64 lastProcessTime = 0;
		static u64 lastWallTime    = 0;

		FILETIME ftCreation, ftExit, ftKernel, ftUser, ftNow;
		if (!GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser))
			return 0.0f;
		GetSystemTimeAsFileTime(&ftNow);

		u64 processTime = FileTimeToU64(ftKernel) + FileTimeToU64(ftUser);
		u64 wallTime    = FileTimeToU64(ftNow);

		f32 usage = 0.0f;
		if (lastWallTime != 0 && wallTime > lastWallTime)
		{
			u64 processDelta = processTime - lastProcessTime;
			u64 wallDelta    = wallTime - lastWallTime;
			usage = static_cast<f32>(static_cast<f64>(processDelta) / static_cast<f64>(wallDelta));
		}
		lastProcessTime = processTime;
		lastWallTime    = wallTime;
		return usage;
	}
}

#endif
