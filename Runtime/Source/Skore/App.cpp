// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "App.hpp"

#include <mutex>
#include <SDL3/SDL.h>

#include "Events.hpp"
#include "RegisterTypes.hpp"
#include "Core/ArgParser.hpp"
#include "Core/Logger.hpp"
#include "Core/Reflection.hpp"
#include "Core/Settings.hpp"
#include "Graphics/Graphics.hpp"
#include "IO/FileSystem.hpp"
#include "IO/InputTypes.hpp"
#include "IO/Path.hpp"

namespace Skore
{

	typedef void(*FnSDLEventCallback)(SDL_Event* event);

	void InputInit();
	Key  FromSDL(u32 key);
	void ReflectionSetReadOnly(bool readOnly, bool enableReload);
	bool GraphicsInit(const AppConfig& appConfig);
	void PhysicsInit();
	void PhysicsShutdown();
	void GraphicsShutdown();
	bool GraphicsUpdate();
	void ResourceInit();
	void ResourceShutdown();
	void AudioEngineInit();
	void AudioEngineShutdown();
	bool GraphicsHandleEvents(SDL_Event* event);
	void InputHandlerEvents(SDL_Event* event);

	void CreateGraphicsDefaultValues();

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

		bool initialized = false;
		bool running = false;
		bool typesRegistered = false;
		bool requireShutdown = false;
		bool enableReload = false;

		ArgParser argParser;

		EventHandler<OnInit>             onInitHandler{};
		EventHandler<OnUpdate>           onUpdateHandler{};
		EventHandler<OnBeginFrame>       onBeginFrameHandler{};
		EventHandler<OnEndFrame>         onEndFrameHandler{};
		EventHandler<OnShutdown>         onShutdownHandler{};
		EventHandler<OnShutdownRequest>  onShutdownRequest{};
		EventHandler<OnDropFileCallback> onDropFileCallback{};

		Array<SDL_SharedObject*> plugLibraries;

		std::mutex mutex;
		Array<std::function<void()>> funcs;

		Logger& logger = Logger::GetLogger("Skore::App");

		Array<FnSDLEventCallback> eventCallbacks;
	}

	SK_API void AddSDLEventCallback(FnSDLEventCallback callback)
	{
		eventCallbacks.EmplaceBack(callback);
	}

	AppResult AppEvents()
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{

			for (auto& callback : eventCallbacks)
			{
				callback(&event);
			}

			if (!GraphicsHandleEvents(&event))
			{
				return AppResult::Failure;
			}

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

		onShutdownHandler.Invoke();

		AudioEngineShutdown();
		PhysicsShutdown();
		GraphicsShutdown();
		ResourceShutdown();

		for (auto lib : plugLibraries)
		{
			SDL_UnloadObject(lib);
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

		onBeginFrameHandler.Invoke();
		onUpdateHandler.Invoke();

		if (!GraphicsUpdate())
		{
			return AppResult::Failure;
		}

		onEndFrameHandler.Invoke();

		frame++;

		return AppResult::Continue;
	}

	AppResult App::Init(const AppConfig& appConfig, int argc, char** argv)
	{

		SK_ASSERT(!running, "App cannot be initialized twice");

		if (!typesRegistered)
		{
			logger.Error("Types are not registered, please call AppTypeRegister before calling AppInit");
			SK_ASSERT(false, "Types are not registered, please call AppTypeRegister before calling AppInit");
			return AppResult::Failure;
		}

		enableReload = appConfig.enableReload;

		argParser.Parse(argc, argv);

		if (argParser.Has("export-api"))
		{
			String apiPath = argParser.Get("export-api");
			Reflection::Export(!apiPath.Empty() ? Path::Join(apiPath, "skore-api.json") : Path::Join(FileSystem::CurrentDir(), "skore-api.json"));
			return AppResult::Success;
		}

		ReflectionSetReadOnly(true, appConfig.enableReload);
		ResourceInit();

		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
		{
			logger.Error("error or SDL_Init {} ", SDL_GetError());
			return AppResult::Failure;
		}

		InputInit();
		if (!GraphicsInit(appConfig))
		{
			return AppResult::Failure;
		}

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
		AppResult result;

		while (running)
		{
			result = AppEvents();
			if (result != AppResult::Continue)
			{
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

	void App::TypeRegister()
	{
		TypeRegister(nullptr);
	}

	void App::TypeRegister(FnTypeRegisterCallback callback)
	{
		RegisterTypes();
		if (callback)
		{
			callback();
		}
		typesRegistered = true;
	}

	void App::RequestShutdown()
	{
		bool canChose = true;
		onShutdownRequest.Invoke(&canChose);
		if (canChose)
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

	ArgParser& App::GetArgs()
	{
		return argParser;
	}

	void App::RunOnMainThread(const std::function<void()>& callback)
	{
		std::lock_guard lock(mutex);
		funcs.EmplaceBack(callback);
	}

	void App::LoadPlugin(StringView path)
	{
		if (SDL_SharedObject* library = SDL_LoadObject(path.CStr()))
		{

			if (auto func = SDL_LoadFunction(library, "SkoreLoadPlugin"))
			{
				ReflectionSetReadOnly(false, false);
				func();
				ReflectionSetReadOnly(true, enableReload);
			}
			plugLibraries.EmplaceBack(library);
		}
	}
}
