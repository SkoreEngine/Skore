#pragma once

#include <functional>
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	class ArgParser;

	enum class AppResult
	{
		Continue,
		Success,
		Failure,
	};


	struct AppSettings
	{
		enum
		{
			Title,
			Size,
			Maximized,
			Fullscreen,
		};
	};

	struct AppConfig
	{
		String title;
		u32    width;
		u32    height;
		bool   maximized;
		bool   fullscreen;
		bool   enableReload = false;
	};

	typedef void (*FnInitCallback)();

	struct SK_API App
	{
		static void      Init(int argc, char** argv);
		static void      Init(int argc, char** argv, FnInitCallback callback);

		static AppResult CreateContext(const AppConfig& appConfig);
		static AppResult Run();

		static void       RequestShutdown();
		static void       ResetContext();
		static f64        DeltaTime();
		static u64        Frame();
		static f32				GetFPS();
		static f32				GetFrameTime();
		static ArgParser& GetArgs();
		static void       SetTargetFPS(u32 fps); // 0 = unlimited
		static u32        GetTargetFPS();
		static void       RunOnMainThread(const std::function<void()>& callback);
		static void       LoadPlugin(StringView path);
		static bool       ReloadedEnabled();
		static void       SetReloadEnabled(bool enabled);


		static void RegisterType(NativeReflectType<App>& type);
	};
}
