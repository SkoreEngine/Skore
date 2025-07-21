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

	struct AppConfig
	{
		String title;
		u32    width;
		u32    height;
		bool   maximized;
		bool   fullscreen;
		bool   enableReload = false;
	};

	typedef void (*FnTypeRegisterCallback)();

	struct SK_API App
	{
		static AppResult Init(const AppConfig& appConfig, int argc, char** argv);
		static AppResult Run();
		static void      TypeRegister();
		static void      TypeRegister(FnTypeRegisterCallback callback);

		static void       RequestShutdown();
		static void       ResetContext();
		static f64        DeltaTime();
		static u64        Frame();
		static ArgParser& GetArgs();
		static void       RunOnMainThread(const std::function<void()>& callback);
		static void		  LoadPlugin(StringView path);
	};
}
