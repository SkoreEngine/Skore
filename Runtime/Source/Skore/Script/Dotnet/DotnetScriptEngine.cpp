#include "DotnetScriptEngine.hpp"

#include "HostfxrResolver.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/ReflectionApi.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Platform/Platform.hpp"

#include "coreclr_delegates.h"
#include "hostfxr.h"

#include <SDL3/SDL.h>

#if SK_WIN
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <Windows.h>
#endif

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::DotnetScriptEngine");

		Array<char_t> ToCharT(StringView value)
		{
			Array<char_t> result{};
#if SK_WIN
			i32 required = MultiByteToWideChar(CP_UTF8, 0, value.Data(), static_cast<i32>(value.Size()), nullptr, 0);
			result.Resize(static_cast<usize>(required) + 1);
			if (required > 0)
			{
				MultiByteToWideChar(CP_UTF8, 0, value.Data(), static_cast<i32>(value.Size()), result.Data(), required);
			}
			result[required] = 0;
#else
			result.Resize(value.Size() + 1);
			for (usize i = 0; i < value.Size(); ++i)
			{
				result[i] = value.Data()[i];
			}
			result[value.Size()] = 0;
#endif
			return result;
		}
	}

	void DotnetScriptEngine::Init()
	{
		String dotnetRoot{};
		String fxrPath{};
		if (!HostfxrResolver::TryGetPath(dotnetRoot, fxrPath))
		{
			logger.Error("could not locate the .NET runtime (hostfxr), install the .NET runtime or set DOTNET_ROOT");
			return;
		}

		logger.Debug("dotnet runtime found, root: '{}', hostfxr: '{}'", dotnetRoot, fxrPath);

		m_hostfxrLibrary = Platform::LoadObject(fxrPath);
		if (m_hostfxrLibrary == nullptr)
		{
			logger.Error("failed to load hostfxr library: '{}'", fxrPath);
			return;
		}

		auto initForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(Platform::LoadFunction(m_hostfxrLibrary, "hostfxr_initialize_for_runtime_config"));
		auto getRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(Platform::LoadFunction(m_hostfxrLibrary, "hostfxr_get_runtime_delegate"));
		auto closeContext = reinterpret_cast<hostfxr_close_fn>(Platform::LoadFunction(m_hostfxrLibrary, "hostfxr_close"));

		if (initForRuntimeConfig == nullptr || getRuntimeDelegate == nullptr || closeContext == nullptr)
		{
			logger.Error("failed to resolve required hostfxr exports");
			return;
		}

		const char* basePathRaw = SDL_GetBasePath();
		String      basePath = basePathRaw != nullptr ? String{basePathRaw} : FileSystem::CurrentDir();

		String runtimeConfigPath = Path::Join(basePath, "managed", "SkoreEngine.runtimeconfig.json");
		String assemblyPath = Path::Join(basePath, "managed", "SkoreEngine.dll");

		if (!FileSystem::GetFileStatus(runtimeConfigPath).exists)
		{
			logger.Error("managed runtime config not found: '{}'", runtimeConfigPath);
			return;
		}

		Array<char_t>  runtimeConfigPathChars = ToCharT(runtimeConfigPath);
		hostfxr_handle context = nullptr;
		if (i32 rc = initForRuntimeConfig(runtimeConfigPathChars.Data(), nullptr, &context); rc != 0 || context == nullptr)
		{
			logger.Error("hostfxr_initialize_for_runtime_config failed: {:#x}", static_cast<u32>(rc));
			if (context != nullptr)
			{
				closeContext(context);
			}
			return;
		}

		load_assembly_and_get_function_pointer_fn loadAssembly = nullptr;
		if (i32 rc = getRuntimeDelegate(context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&loadAssembly)); rc != 0 || loadAssembly == nullptr)
		{
			logger.Error("hostfxr_get_runtime_delegate failed: {:#x}", static_cast<u32>(rc));
			closeContext(context);
			return;
		}

		m_hostContext = context;
		m_loadAssemblyAndGetFunctionPointer = reinterpret_cast<VoidPtr>(loadAssembly);

		Array<char_t> assemblyPathChars = ToCharT(assemblyPath);
		Array<char_t> typeNameChars = ToCharT("Skore.EntryPoint, SkoreEngine");
		Array<char_t> methodNameChars = ToCharT("Bootstrap");

		component_entry_point_fn bootstrap = nullptr;
		if (i32 rc = loadAssembly(assemblyPathChars.Data(), typeNameChars.Data(), methodNameChars.Data(), nullptr, nullptr, reinterpret_cast<void**>(&bootstrap)); rc != 0 || bootstrap == nullptr)
		{
			logger.Error("failed to load managed entry point 'Skore.EntryPoint.Bootstrap': {:#x}", static_cast<u32>(rc));
			return;
		}

		const ReflectionApi* reflectionApi = GetReflectionApi();
		i32                  result = bootstrap(const_cast<ReflectionApi*>(reflectionApi), static_cast<i32>(sizeof(ReflectionApi)));
		logger.Debug("managed entry point 'Skore.EntryPoint.Bootstrap' invoked, returned {}", result);
	}
}
