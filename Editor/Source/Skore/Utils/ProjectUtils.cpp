#include "Skore/Utils/ProjectUtils.hpp"

#include "Skore/Core/Array.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Platform/Platform.hpp"


namespace Skore
{

	static Logger& logger = Logger::GetLogger("Skore::ProjectUtils");

	bool HasCMakeProject(StringView directory)
	{
		bool exists = FileSystem::GetFileStatus(Path::Join(directory, "CMakeLists.txt")).exists;
		return exists;
	}

	void CreateCMakeProject(StringView directory)
	{
		FileSystem::CreateDirectory(Path::Join(directory, "Source"));
		FileSystem::CreateDirectory(Path::Join(Path::Join(directory, "Binaries"), "Runtime"));

		String projectName = Path::Name(directory);
		String projectUpper = ToUpper(projectName);

		String entryPointFile = projectName;
		entryPointFile[0] = toupper(entryPointFile[0]);

		String cmakeSource;

		cmakeSource += "#CMakeLists.txt\n";
		cmakeSource += "cmake_minimum_required(VERSION 3.30)\n\n";
		cmakeSource += "project(" + projectName + ")\n\n";
		cmakeSource += "set(CMAKE_CXX_STANDARD 20)\n\n";
		cmakeSource += "include(FetchContent)\n";
		cmakeSource += "FetchContent_Declare(\n";
		cmakeSource += "	Skore\n";
		cmakeSource += "	GIT_REPOSITORY https://github.com/SkoreEngine/Skore\n";
		cmakeSource += "	GIT_TAG main\n";
		cmakeSource += ")\n";
		cmakeSource += "FetchContent_MakeAvailable(Skore)\n\n";
		cmakeSource += "file(GLOB_RECURSE " + projectUpper + "_RUNTIME_SOURCES Source/*.hpp Source/*.cpp Source/*.h Source/*.c) \n";
		cmakeSource += "add_library(" + projectName + " SHARED ${" + projectUpper + "_RUNTIME_SOURCES})\n";
		cmakeSource += "target_link_libraries(" + projectName + " SkoreRuntime)\n";
		cmakeSource += "target_include_directories(" + projectName + " PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)\n\n";

		cmakeSource += "set_target_properties(" + projectName + " PROPERTIES \n";
		cmakeSource += "	RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries/Runtime\"\n";
		cmakeSource += "	LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries/Runtime\"\n";
		cmakeSource += "	ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries/Runtime\"\n";
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

	bool HasDotnetProject(StringView directory)
	{
		String projectName = Path::Name(directory);
		bool   exists = FileSystem::GetFileStatus(Path::Join(directory, projectName + ".csproj")).exists;
		return exists;
	}

	void WriteDotnetEngineProps(StringView directory)
	{
		String enginePath = Path::Join(FileSystem::CurrentDir(), "managed");
		for (usize i = 0; i < enginePath.Size(); ++i)
		{
			if (enginePath[i] == '\\')
			{
				enginePath[i] = '/';
			}
		}

		String props;
		props += "<Project>\n";
		props += "	<PropertyGroup>\n";
		props += "		<SkoreEnginePath>" + enginePath + "</SkoreEnginePath>\n";
		props += "	</PropertyGroup>\n";
		props += "</Project>\n";

		FileSystem::SaveFileAsString(Path::Join(directory, "Intermediate", "Scripting", "SkoreEngine.props"), props);
	}

	void CreateDotnetProject(StringView directory)
	{
		String projectName = Path::Name(directory);

		String csproj;
		csproj += "<Project>\n\n";
		csproj += "	<PropertyGroup>\n";
		csproj += "		<BaseIntermediateOutputPath>Intermediate/Scripting/obj/</BaseIntermediateOutputPath>\n";
		csproj += "	</PropertyGroup>\n\n";
		csproj += "	<Import Project=\"Sdk.props\" Sdk=\"Microsoft.NET.Sdk\" />\n\n";
		csproj += "	<Import Project=\"Intermediate/Scripting/SkoreEngine.props\" Condition=\"Exists('Intermediate/Scripting/SkoreEngine.props')\" />\n\n";
		csproj += "	<PropertyGroup>\n";
		csproj += "		<TargetFramework>net10.0</TargetFramework>\n";
		csproj += "		<RootNamespace>" + projectName + "</RootNamespace>\n";
		csproj += "		<AssemblyName>" + projectName + "</AssemblyName>\n";
		csproj += "		<ImplicitUsings>enable</ImplicitUsings>\n";
		csproj += "		<Nullable>enable</Nullable>\n";
		csproj += "		<AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n";
		csproj += "		<EnableDynamicLoading>true</EnableDynamicLoading>\n";
		csproj += "		<AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n";
		csproj += "		<AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>\n";
		csproj += "		<OutputPath>Intermediate/Scripting/</OutputPath>\n";
		csproj += "		<EnableDefaultNoneItems>false</EnableDefaultNoneItems>\n";
		csproj += "		<DefaultItemExcludes>$(DefaultItemExcludes);Intermediate/**;Local/**;Packages/**</DefaultItemExcludes>\n";
		csproj += "	</PropertyGroup>\n\n";
		csproj += "	<ItemGroup>\n";
		csproj += "		<Reference Include=\"SkoreEngine\">\n";
		csproj += "			<HintPath>$(SkoreEnginePath)/SkoreEngine.dll</HintPath>\n";
		csproj += "			<Private>false</Private>\n";
		csproj += "		</Reference>\n";
		csproj += "	</ItemGroup>\n\n";
		csproj += "	<Target Name=\"EnsureSkoreEnginePath\" BeforeTargets=\"ResolveAssemblyReferences\" Condition=\"'$(SkoreEnginePath)' == ''\">\n";
		csproj += "		<Error Text=\"SkoreEnginePath is not set. Open this project in the Skore editor to generate Intermediate/Scripting/SkoreEngine.props.\" />\n";
		csproj += "	</Target>\n\n";
		csproj += "	<Import Project=\"Sdk.targets\" Sdk=\"Microsoft.NET.Sdk\" />\n\n";
		csproj += "</Project>\n";

		FileSystem::SaveFileAsString(Path::Join(directory, projectName + ".csproj"), csproj);

		WriteDotnetEngineProps(directory);

		String gitignorePath = Path::Join(directory, ".gitignore");
		if (!FileSystem::GetFileStatus(gitignorePath).exists)
		{
			String gitignore;
			gitignore += "# Skore generated\n";
			gitignore += "/Intermediate/\n";
			gitignore += "/Local/\n";
			FileSystem::SaveFileAsString(gitignorePath, gitignore);
		}
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

	static void RunRider(StringView csprojPath, StringView filePath)
	{
#if defined(SK_WIN)
		const char* launchers[] = {"rider.cmd", "rider.bat", "rider64.exe", "rider.exe"};
#else
		const char* launchers[] = {"rider"};
#endif

		for (const char* launcher : launchers)
		{
			Array<const char*> args;
			args.EmplaceBack(launcher);
			if (!csprojPath.Empty())
			{
				args.EmplaceBack(csprojPath.CStr());
			}
			if (!filePath.Empty())
			{
				args.EmplaceBack(filePath.CStr());
			}
			args.EmplaceBack(nullptr);

			if (VoidPtr process = Platform::CreateProcess(args.Data(), false))
			{
				Platform::DestroyProcess(process);
				return;
			}
		}
	}

	void OpenDotnetProjectInEditor(StringView projectPath)
	{
		String projectName = Path::Name(projectPath);
		String csprojPath = Path::Join(projectPath, projectName + ".csproj");

		if (FileSystem::GetFileStatus(csprojPath).exists)
		{
			RunRider(csprojPath, {});
		}
	}

	void OpenDotnetFileInEditor(StringView projectPath, StringView filePath)
	{
		String projectName = Path::Name(projectPath);
		String csprojPath = Path::Join(projectPath, projectName + ".csproj");
		String file = filePath;

		//passing the project first makes rider reuse the window when the project is already open
		if (FileSystem::GetFileStatus(csprojPath).exists)
		{
			RunRider(csprojPath, file);
		}
		else
		{
			RunRider({}, file);
		}
	}
}
