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

#include "Skore/Main.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Project/ProjectManager.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Sinks.hpp"

namespace Skore
{
	void EditorInit(StringView project);
	void EditorTypeRegister();
	void RegisterProjectManagerTypes();
	ConsoleSink& GetConsoleSink();
	StdOutSink stdOutSink{};

	void ImGuiInit();
	void ImGuiNewFrame();

	i32 Main(int argc, char** argv)
	{
		Logger::RegisterSink(stdOutSink);
		Logger::RegisterSink(GetConsoleSink());

		App::TypeRegister([]
		{
			GroupScope scope("Editor");
			EditorTypeRegister();
			RegisterProjectManagerTypes();
		});

		AppConfig appConfig;
		appConfig.title = "Skore Editor";
		appConfig.fullscreen = false;
		appConfig.maximized = false;
		appConfig.width = 1280;
		appConfig.height = 720;
		appConfig.enableReload = true;

		ArgParser argParser;
		argParser.Parse(argc, argv);
		String initialProject;

		if (argParser.Has("project"))
		{
			initialProject = argParser.Get("project");
			appConfig.maximized = true;
		}

		if (App::Init(appConfig, argc, argv) != AppResult::Continue)
		{
			return 1;
		}

		ImGuiInit();

		if (!initialProject.Empty())
		{
			EditorInit(initialProject);
		}
		else
		{
			ProjectManager::Init();
		}

		if (App::Run() == AppResult::Failure)
		{
			return 1;
		}

		return 0;
	}
}