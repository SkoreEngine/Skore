#include "Skore/Platform/Platform.hpp"

#define SDL_FUNCTION_POINTER_IS_VOID_POINTER
#include <mutex>
#include <SDL3/SDL.h>
#include "SDL3/SDL_vulkan.h"

#include "volk.h"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Devices/Vulkan/VulkanPlatform.hpp"

namespace Skore
{
	EventHandler<OnWindowResized>   onWindowResized{};
	EventHandler<OnWindowMinimized> onWindowMinimized{};
	EventHandler<OnWindowRestored>  onWindowRestored{};

	typedef void (*FnSDLEventCallback)(SDL_Event* event, VoidPtr userData);
	void           AddSDLEventCallback(FnSDLEventCallback callback, VoidPtr userData);

	struct SDLWindowHandler
	{
		SDL_Window* window;
		Vec2        mousePosBeforeHideCursor = Vec2{0.0f, 0.0f};
	};


	SDL_Window* GetSDLWindowFromHandler(Window window)
	{
		if (window)
			return window.ToPtr<SDLWindowHandler>()->window;
		return nullptr;
	}


	std::mutex                   mutex;
	Array<std::function<void()>> runOnMainThread;

	void PlatformProcessEvents()
	{
		std::lock_guard lock(mutex);
		for (auto& callback : runOnMainThread)
		{
			callback();
		}
		runOnMainThread.Clear();
	}

	void SDLEventCallback(SDL_Event* event, VoidPtr userData)
	{
		SDLWindowHandler* handler = static_cast<SDLWindowHandler*>(userData);

		switch (event->type)
		{
			case SDL_EVENT_WINDOW_RESIZED:
			{
				if (SDL_GetWindowID(handler->window) == event->window.windowID)
				{
					onWindowResized.Invoke(handler->window);
				}
				break;
			}
			case SDL_EVENT_WINDOW_MAXIMIZED:
			{
				break;
			}
			case SDL_EVENT_WINDOW_MINIMIZED:
			{
				if (SDL_GetWindowID(handler->window) == event->window.windowID)
				{
					onWindowMinimized.Invoke(handler->window);
				}
				break;
			}
			case SDL_EVENT_WINDOW_RESTORED:
			{
				if (SDL_GetWindowID(handler->window) == event->window.windowID)
				{
					onWindowRestored.Invoke(handler->window);
				}
				break;
			}
		}
	}
	
	Window Platform::CreateWindow(StringView title, u32 width, u32 height, WindowFlags flags)
	{
		SDL_WindowFlags sdlFlags = 0;
		sdlFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		f32 scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

		width *= scale;
		height *= scale;

		if (flags && WindowFlags::Fullscreen)
		{
			sdlFlags |= SDL_WINDOW_FULLSCREEN;
		}

		if (flags && WindowFlags::Maximized)
		{
			sdlFlags |= SDL_WINDOW_MAXIMIZED;
		}

		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
				sdlFlags |= SDL_WINDOW_VULKAN;
				break;
			case GraphicsAPI::D3D12:
				break;
			case GraphicsAPI::Metal:
				sdlFlags |= SDL_WINDOW_METAL;
				break;
			case GraphicsAPI::None:
				break;
		}

		SDLWindowHandler* handler = Alloc<SDLWindowHandler>();
		handler->window = SDL_CreateWindow(title.CStr(), static_cast<i32>(width), static_cast<i32>(height), sdlFlags);;
		AddSDLEventCallback(SDLEventCallback, handler);

		return {handler};
	}

	void Platform::DestroyWindow(Window window)
	{
		SDLWindowHandler* handler = window.ToPtr<SDLWindowHandler>();
		SDL_DestroyWindow(handler->window);
		DestroyAndFree(handler);
	}


	f32 Platform::GetWindowDPI(Window window)
	{
		return SDL_GetWindowDisplayScale(GetSDLWindowFromHandler(window));
	}

	Extent Platform::GetWindowSize(Window window)
	{
		i32 width, height;
		SDL_GetWindowSize(GetSDLWindowFromHandler(window), &width, &height);
		return {static_cast<u32>(width), static_cast<u32>(height)};
	}

	bool Platform::IsWindowMinimized(Window window)
	{
		return SDL_GetWindowFlags(GetSDLWindowFromHandler(window)) & SDL_WINDOW_MINIMIZED;
	}

	void Platform::SetWindowCursorLockMode(Window window, CursorLockMode cursorLockMode)
	{
		SDLWindowHandler* handler = window.ToPtr<SDLWindowHandler>();

		switch (cursorLockMode)
		{
			case CursorLockMode::Free:
			{
				SDL_SetWindowRelativeMouseMode(handler->window, false);
				SDL_WarpMouseGlobal(handler->mousePosBeforeHideCursor.x, handler->mousePosBeforeHideCursor.y);
				break;
			}
			case CursorLockMode::Locked:
			{
				SDL_GetGlobalMouseState(&handler->mousePosBeforeHideCursor.x, &handler->mousePosBeforeHideCursor.y);
				SDL_SetWindowRelativeMouseMode(handler->window, true);
				break;
			}
		}
	}

	void Platform::MaximizeWindow(Window window)
	{
		SDL_MaximizeWindow(GetSDLWindowFromHandler(window));
	}

	void Platform::SetWindowIcon(Window window, const u8* rgbaPixels, i32 width, i32 height)
	{
		if (!rgbaPixels || width <= 0 || height <= 0) return;

		SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<u8*>(rgbaPixels), width * 4);
		if (!surface) return;

		SDL_SetWindowIcon(GetSDLWindowFromHandler(window), surface);
		SDL_DestroySurface(surface);
	}

	VoidPtr Platform::GetNativeWindowHandle(Window window)
	{
		return GetSDLWindowFromHandler(window);
	}

	bool Platform::ShowSimpleMessageBox(MessageBoxType type, StringView title, StringView message, Window window)
	{
		SDL_MessageBoxFlags flags = 0;

		switch (type)
		{
			case MessageBoxType::Info:
				flags = SDL_MESSAGEBOX_INFORMATION;
				break;
			case MessageBoxType::Warning:
				flags = SDL_MESSAGEBOX_WARNING;
				break;
			case MessageBoxType::Error:
				flags = SDL_MESSAGEBOX_ERROR;
				break;
		}

		return SDL_ShowSimpleMessageBox(flags, title.CStr(), message.CStr(), GetSDLWindowFromHandler(window));
	}

	void Platform::SetVulkanLoader(PFN_vkGetInstanceProcAddr procAddr)
	{
		SDL_Vulkan_LoadLibrary(nullptr);
	}

	Span<const char* const> Platform::GetRequiredInstanceExtensions()
	{
		u32  extensionCount = 0;
		auto extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
		return Span{extensions, extensionCount};
	}

	bool Platform::GetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, u32 queueFamily)
	{
		return SDL_Vulkan_GetPresentationSupport(instance, device, queueFamily);
	}

	bool Platform::CreateWindowSurface(Window window, VkInstance instance, VkSurfaceKHR* surface)
	{
		return SDL_Vulkan_CreateSurface(GetSDLWindowFromHandler(window), instance, nullptr, surface);
	}

	VoidPtr Platform::LoadObject(StringView library)
	{
		return SDL_LoadObject(library.CStr());
	}

	void Platform::UnloadObject(VoidPtr library)
	{
		SDL_UnloadObject(static_cast<SDL_SharedObject*>(library));
	}

	VoidPtr Platform::LoadFunction(VoidPtr library, StringView functionName)
	{
		return SDL_LoadFunction(static_cast<SDL_SharedObject*>(library), functionName.CStr());
	}

	void Platform::SetClipboardText(StringView text)
	{
		SDL_SetClipboardText(text.CStr());
	}

	String Platform::GetClipboardText()
	{
		if (char* text = SDL_GetClipboardText())
		{
			String ret = text;
			SDL_free(text);
			return ret;
		}
		return {};
	}


	void Platform::OpenURL(StringView url)
	{
		SDL_OpenURL(url.CStr());
	}

	VoidPtr Platform::CreateProcess(const char* const* args, bool pipeStdio, bool background)
	{
		if (!background)
		{
			return SDL_CreateProcess(args, pipeStdio);
		}

		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<void*>(static_cast<const void*>(args)));
		if (pipeStdio)
		{
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
		}
		SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);

		SDL_Process* process = SDL_CreateProcessWithProperties(props);
		SDL_DestroyProperties(props);
		return process;
	}

	String Platform::ReadProcess(VoidPtr process, i32* exitCode)
	{
		if (process == nullptr)
		{
			if (exitCode)
			{
				*exitCode = -1;
			}
			return {};
		}

		usize size = 0;
		int   code = 0;
		void* data = SDL_ReadProcess(static_cast<SDL_Process*>(process), &size, &code);

		if (exitCode)
		{
			*exitCode = code;
		}

		String output;
		if (data)
		{
			output = String(static_cast<const char*>(data), size);
			SDL_free(data);
		}
		return output;
	}

	void Platform::DestroyProcess(VoidPtr process)
	{
		SDL_DestroyProcess(static_cast<SDL_Process*>(process));
	}

	u64 Platform::GetTime()
	{
		SDL_Time time = {};
		SDL_GetCurrentTime(&time);
		return time;
	}

#if !defined(SK_WIN)
	ProcessMemoryInfo Platform::GetProcessMemoryInfo()
	{
		return {};
	}

	SystemMemoryInfo Platform::GetSystemMemoryInfo()
	{
		SystemMemoryInfo info{};
		info.totalBytes = static_cast<u64>(SDL_GetSystemRAM()) * 1024ull * 1024ull;
		return info;
	}

	f32 Platform::GetProcessCPUUsage()
	{
		return 0.0f;
	}
#endif

	u32 Platform::GetLogicalCoreCount()
	{
		int n = SDL_GetNumLogicalCPUCores();
		return n > 0 ? static_cast<u32>(n) : 1u;
	}

	void Platform::SaveDialog(std::function<void(StringView path)>&& func, Span<FileFilter> filters, StringView defaultPath, StringView fileName, Window window) {}


	void Platform::OpenDialog(std::function<void(StringView path)>&& func, Span<FileFilter> filters, StringView defaultPath, Window window)
	{
		auto funcPtr = Alloc<std::function<void(StringView)>>(func);
		auto callback = [](void* userdata, const char* const * filelist, int filter)
		{
			if (filelist && filelist[0])
			{
				String          path = filelist[0];
				std::lock_guard lock(mutex);
				runOnMainThread.EmplaceBack([userdata, path]
				{
					std::function<void(StringView)>& func = *static_cast<std::function<void(StringView)>*>(userdata);
					func(path);
					DestroyAndFree(&func);
				});
			}
		};

		SDL_ShowOpenFileDialog(callback, funcPtr, GetSDLWindowFromHandler(window), reinterpret_cast<const SDL_DialogFileFilter*>(filters.Data()), 1, defaultPath.CStr(), false);
	}

	void Platform::OpenDialogMultiple(std::function<void(Span<StringView> path)>&& func, Span<FileFilter> filters, StringView defaultPath, Window window)
	{
		auto funcPtr = Alloc<std::function<void(Span<StringView> path)>>(func);
		auto callback = [](void* userdata, const char* const * filelist, int filter)
		{
			if (filelist)
			{
				Array<String> pathStr;
				for (int i = 0; filelist[i]; i++)
				{
					pathStr.EmplaceBack(filelist[i]);
				}

				std::lock_guard lock(mutex);
				runOnMainThread.EmplaceBack([userdata, pathStr]
				{
					Array<StringView> paths;
					for (auto& path : pathStr)
					{
						paths.EmplaceBack(path);
					}
					std::function<void(Span<StringView> path)>& func = *static_cast<std::function<void(Span<StringView> path)>*>(userdata);
					func(paths);
					DestroyAndFree(&func);
				});
			}
		};

		SDL_ShowOpenFileDialog(callback, funcPtr, GetSDLWindowFromHandler(window), reinterpret_cast<const SDL_DialogFileFilter*>(filters.Data()), 1, defaultPath.CStr(), true);
	}

	void Platform::PickFolder(std::function<void(StringView path)>&& func, StringView defaultPath, Window window)
	{
		auto funcPtr = Alloc<std::function<void(StringView)>>(func);
		auto callback = [](void* userdata, const char* const* filelist, int filter)
		{
			if (filelist && filelist[0])
			{
				String          path = filelist[0];
				std::lock_guard lock(mutex);
				runOnMainThread.EmplaceBack([userdata, path]
				{
					std::function<void(StringView)>& func = *static_cast<std::function<void(StringView)>*>(userdata);
					func(path);
					DestroyAndFree(&func);
				});
			}
		};

		SDL_ShowOpenFolderDialog(callback, funcPtr, GetSDLWindowFromHandler(window), defaultPath.CStr(), false);
	}
}