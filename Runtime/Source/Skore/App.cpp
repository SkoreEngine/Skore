#include "Skore/App.hpp"

#include <mutex>
#include <SDL3/SDL.h>

#include "Skore/Events.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/RegisterTypes.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/DebugUtils.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/InputTypes.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{
	typedef void (*FnSDLEventCallback)(SDL_Event* event, VoidPtr userData);

	void InputInit();
	void FileSystemInit();
	Key  FromSDL(u32 key);
	void ReflectionSetOptions(bool reloadEnabled);
	bool GraphicsInit(const AppConfig& appConfig);
	void PhysicsInit();
	void PhysicsShutdown();
	void GraphicsShutdown();
	void ResourceInit();
	void ResourceShutdown();
	void AudioEngineInit();
	void AudioEngineShutdown();
	void InputHandlerEvents(SDL_Event* event);
	void ScriptEngineInit();
	void LayerSystemInit();
	void LayerSystemShutdown();

	void PlatformProcessEvents();

	void CreateGraphicsDefaultValues();

	//openXR
	void      OpenXRManagerCreateSession();
	AppResult OpenXrManagerIterate();

	namespace
	{
		u64 frame{0};
		u64 lastFrameTime;
		u64 perfFrequency;
		f64 deltaTime = 0.0f;

		// FPS calculation variables
		Uint32 frameCount = 0;
		f32    fpsTimer = 0.0f;
		f32    fps = 0.0f;

		u32 targetFPS = 0; // 0 = unlimited

		bool initialized = false;
		bool running = false;
		bool appInitialized = false;
		bool requireShutdown = false;

		ArgParser argParser;

		EventHandler<OnInit>             onInitHandler{};
		EventHandler<OnUpdate>           onUpdateHandler{};
		EventHandler<OnBeginFrame>       onBeginFrameHandler{};
		EventHandler<OnEndFrame>         onEndFrameHandler{};
		EventHandler<OnShutdown>         onShutdownHandler{};
		EventHandler<OnShutdownRequest>  onShutdownRequest{};
		EventHandler<OnDropFileCallback> onDropFileCallback{};
		EventHandler<OnPluginReloaded>   onPluginReloaded{};

		Array<VoidPtr>  plugLibraries;
		HashSet<String> loadedPluginPaths;
		bool            enableReload = false;

		std::mutex                   mutex;
		Array<std::function<void()>> funcs;

		Logger& logger = Logger::GetLogger("Skore::App");

		Array<Pair<FnSDLEventCallback, VoidPtr>> eventCallbacks;
	}

	void App::Init(int argc, char** argv)
	{
		Init(argc, argv, nullptr);
	}

	void App::Init(int argc, char** argv, FnInitCallback callback)
	{
		argParser.Parse(argc, argv);

		FileSystemInit();
		ResourceInit();
		RegisterTypes();
		InputInit();
		ScriptEngineInit();
		LayerSystemInit();

		if (callback)
		{
			callback();
		}

		appInitialized = true;
	}

	SK_API void AddSDLEventCallback(FnSDLEventCallback callback, VoidPtr userData)
	{
		eventCallbacks.EmplaceBack(callback, userData);
	}

	AppResult AppEvents()
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			for (auto& callback : eventCallbacks)
			{
				callback.first(&event, callback.second);
			}

			PlatformProcessEvents();
			InputHandlerEvents(&event);

			switch (event.type)
			{
				case SDL_EVENT_QUIT:
					App::RequestShutdown();
					break;
				case SDL_EVENT_DROP_FILE:
					onDropFileCallback.Invoke(event.drop.data);
					break;
			}
		}

		return AppResult::Continue;
	}

	void AppDestroy()
	{
		if (!requireShutdown)
		{
			return;
		}

		Graphics::WaitIdle();

		Profiler::Shutdown();
		onShutdownHandler.Invoke();

		AudioEngineShutdown();
		PhysicsShutdown();
		GraphicsShutdown();
		ResourceShutdown();
		LayerSystemShutdown();

		for (VoidPtr lib : plugLibraries)
		{
			Platform::UnloadObject(lib);
		}
		plugLibraries.Clear();

		SDL_Quit();
		Event::Reset();
	}

	AppResult AppIterate()
	{
		{
			std::lock_guard lock(mutex);
			for (auto& callback : funcs)
			{
				callback();
			}
			funcs.Clear();
		}

		if (!running)
		{
			AppDestroy();
			return AppResult::Success;
		}

		if (!initialized)
		{
			Profiler::Init();
			onInitHandler.Invoke();
			initialized = true;

			logger.Info("Skore Engine {} Initialized", SK_VERSION);
		}

		u64 currentFrameTime = SDL_GetPerformanceCounter();
		deltaTime = static_cast<float>(currentFrameTime - lastFrameTime) / perfFrequency;
		lastFrameTime = currentFrameTime;

		frameCount++;
		fpsTimer += deltaTime;

		if (fpsTimer >= 1.0f)
		{
			fps = frameCount / fpsTimer;
			frameCount = 0;
			fpsTimer = 0.0f;
		}

		//logger.Info("fps {} ", fps);

		Profiler::BeginFrame();
		onBeginFrameHandler.Invoke();
		onUpdateHandler.Invoke();
		onEndFrameHandler.Invoke();
		Profiler::EndFrame();

		frame++;

		if (targetFPS > 0)
		{
			f64 targetFrameTime = 1.0 / static_cast<f64>(targetFPS);
			u64 frameEndTime = SDL_GetPerformanceCounter();
			f64 elapsed = static_cast<f64>(frameEndTime - lastFrameTime) / perfFrequency;
			f64 remaining = targetFrameTime - elapsed;
			if (remaining > 0.0)
			{
				SDL_Delay(static_cast<u32>(remaining * 1000.0));
			}
		}
		return AppResult::Continue;
	}

	AppResult App::CreateContext(const AppConfig& appConfig)
	{
		SK_ASSERT(!running, "App cannot be initialized twice");

		if (!appInitialized)
		{
			logger.Error("App is not initialized, please call Init before calling CreateContext");
			return AppResult::Failure;
		}

		ReflectionSetOptions(appConfig.enableReload);
		enableReload = appConfig.enableReload;

		if (argParser.Has("export-api"))
		{
			String apiPath = argParser.Get("export-api");
			Reflection::Export(!apiPath.Empty() ? Path::Join(apiPath, "skore-api.json") : Path::Join(FileSystem::CurrentDir(), "skore-api.json"));
			return AppResult::Success;
		}

		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
		{
			logger.Error("error or SDL_Init {} ", SDL_GetError());
			return AppResult::Failure;
		}

		if (!GraphicsInit(appConfig))
		{
			return AppResult::Failure;
		}

		OpenXRManagerCreateSession();

		lastFrameTime = SDL_GetPerformanceCounter();
		perfFrequency = SDL_GetPerformanceFrequency();

		running = true;
		requireShutdown = true;

		CreateGraphicsDefaultValues();
		PhysicsInit();
		AudioEngineInit();

		return AppResult::Continue;
	}

	AppResult App::Run()
	{
		AppResult result = AppResult::Success;
		while (running)
		{
			result = AppEvents();
			if (result != AppResult::Continue)
			{
				break;
			}

			result = OpenXrManagerIterate();
			if (result != AppResult::Continue)
			{
				AppDestroy();
				break;
			}

			result = AppIterate();
			if (result != AppResult::Continue)
			{
				break;
			}
		}
		return result;
	}

	void App::RequestShutdown()
	{
		bool canClose = true;
		onShutdownRequest.Invoke(&canClose);
		if (canClose)
		{
			running = false;
		}
	}

	void ReflectionResetContext();

	void App::ResetContext()
	{
		SK_ASSERT(!running, "reset cannot be executed on a running app");
		ReflectionResetContext();
		RegisterTypes();
	}

	f64 App::DeltaTime()
	{
		return deltaTime;
	}

	u64 App::Frame()
	{
		return frame;
	}

	f32 App::GetFPS()
	{
		return fps;
	}

	f32 App::GetFrameTime()
	{
		return fpsTimer;
	}

	ArgParser& App::GetArgs()
	{
		return argParser;
	}

	void App::SetTargetFPS(u32 fps)
	{
		targetFPS = fps;
	}

	u32 App::GetTargetFPS()
	{
		return targetFPS;
	}

	void App::RunOnMainThread(const std::function<void()>& callback)
	{
		std::lock_guard lock(mutex);
		funcs.EmplaceBack(callback);
	}

	void App::LoadPlugin(StringView path)
	{
		if (VoidPtr library = Platform::LoadObject(path.CStr()))
		{
			if (auto func = reinterpret_cast<void(*)()>(Platform::LoadFunction(library, "SkoreLoadPlugin")))
			{
				bool isReload = enableReload && loadedPluginPaths.Has(Path::Name(path));
				func();

				if (isReload)
				{
					onPluginReloaded.Invoke();
				}

				if (enableReload)
				{
					loadedPluginPaths.Insert(Path::Name(path));
				}
			}
			plugLibraries.EmplaceBack(library);
		}
	}

	bool App::ReloadedEnabled()
	{
		return enableReload;
	}

	void App::SetReloadEnabled(bool enabled)
	{
		enableReload = enabled;
	}

	void App::RegisterType(NativeReflectType<App>& type)
	{
		type.Function<&App::Frame>("Frame");
	}

	void RegisterAppTypes()
	{
		ResourceType* type = Resources::Type<AppSettings>()
		                     .Field<AppSettings::Title>(ResourceFieldType::String)
		                     .Field<AppSettings::Size>(ResourceFieldType::Vec2)
		                     .Field<AppSettings::Maximized>(ResourceFieldType::Bool)
		                     .Field<AppSettings::Fullscreen>(ResourceFieldType::Bool)
		                     .Attribute<EditableSettings>(EditableSettings{
			                     .path = "Engine/Application",
			                     .type = TypeInfo<ProjectSettings>::ID(),
			                     .order = 0
		                     })
		                     .Build()
		                     .GetResourceType();

		RID rid = Resources::Create<AppSettings>();

		ResourceObject appSettingsObject = Resources::Write(rid);
		appSettingsObject.SetString(AppSettings::Title, "Skore Game");
		appSettingsObject.SetVec2(AppSettings::Size, {1280, 720});
		appSettingsObject.SetBool(AppSettings::Maximized, false);
		appSettingsObject.SetBool(AppSettings::Fullscreen, false);
		appSettingsObject.Commit();
		type->SetDefaultValue(rid);
	}
}
