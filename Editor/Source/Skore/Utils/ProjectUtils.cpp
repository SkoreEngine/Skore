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

#include "ProjectUtils.hpp"

#include "SDL3/SDL.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"


namespace Skore
{

	static Logger& logger = Logger::GetLogger("Skore::ProjectUtils");

	void CreateCMakeProject(StringView directory)
	{
		String engineSourcePath;

#ifdef SK_ROOT_SOURCE_PATH
		engineSourcePath = SK_ROOT_SOURCE_PATH;
#endif

		if (!FileSystem::GetFileStatus(engineSourcePath).exists)
		{
			//TODO maybe fetch content if not found.
			logger.Error("error on create cpp project, skore source directory not found");
			return;
		}

		FileSystem::CreateDirectory(Path::Join(directory, "Source"));
		FileSystem::CreateDirectory(Path::Join(directory, "Binaries"));

		String projectName = Path::Name(directory);
		String projectUpper = ToUpper(projectName);

		String entryPointFile = projectName;
		entryPointFile[0] = toupper(entryPointFile[0]);

		String cmakeSource;

		cmakeSource += "#CMakeLists.txt\n";
		cmakeSource += "cmake_minimum_required(VERSION 3.30)\n\n";
		cmakeSource += "project(" + projectName + ")\n\n";
		cmakeSource += "set(CMAKE_CXX_STANDARD 20)\n\n";
		cmakeSource += "add_subdirectory(\"" + engineSourcePath + "\" Skore)\n\n";
		cmakeSource += "file(GLOB_RECURSE " + projectUpper + "_RUNTIME_SOURCES Source/*.hpp Source/*.cpp Source/*.h Source/*.c) \n";
		cmakeSource += "add_library(" + projectName + " SHARED ${" + projectUpper + "_RUNTIME_SOURCES})\n";
		cmakeSource += "target_link_libraries(" + projectName + " SkoreRuntime)\n";
		cmakeSource += "target_include_directories(" + projectName + " PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)\n\n";

		cmakeSource += "set_target_properties(" + projectName + " PROPERTIES \n";
		cmakeSource += "	RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries\"\n";
		cmakeSource += "	LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries\"\n";
		cmakeSource += "	ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries\"\n";
		cmakeSource += ") ";


		FileSystem::SaveFileAsString(Path::Join(directory, "CMakeLists.txt"), cmakeSource);

		String cppSource;
		cppSource += "#include <Skore/PluginEntryPoint.hpp>\n";
		cppSource += "#include <Skore/Core/Logger.hpp>\n\n\n";
		cppSource += "using namespace Skore;\n\n";
		cppSource += "static Logger& logger = Logger::GetLogger(\"" + entryPointFile + "::PluginEntryPoint\");\n\n";

		cppSource += "void SkoreLoadPlugin()\n";
		cppSource += "{\n";
		cppSource += "	logger.Info(\"Hello " + projectName + " ...\");\n";
		cppSource += "}\n";


		FileSystem::SaveFileAsString(Path::Join(directory, "Source",  entryPointFile, "EntryPoint.cpp"), cppSource);
	}

	void OpenProjectInEditor(StringView projectPath)
	{
		if (FileSystem::GetFileStatus(Path::Join(projectPath, "CMakeLists.txt")).exists)
		{
			//TODO: only clion now, add vs and vscode later
			String command = "clion ";
			command += projectPath;
			command += " &";

			system(command.CStr());
		}
	}
}
