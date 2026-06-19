#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"


namespace Skore
{
	SK_API bool HasCMakeProject(StringView directory);
	SK_API void CreateCMakeProject(StringView directory);
	SK_API bool HasDotnetProject(StringView directory);
	SK_API void CreateDotnetProject(StringView directory);
	SK_API bool BuildDotnetProject(StringView directory);
	SK_API void WriteDotnetEngineProps(StringView directory);
	SK_API void OpenProjectInEditor(StringView projectPath);
	SK_API void OpenDotnetProjectInEditor(StringView projectPath);
	SK_API void OpenDotnetFileInEditor(StringView projectPath, StringView filePath);
}
