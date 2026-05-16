#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"


namespace Skore
{
	SK_API bool HasCMakeProject(StringView directory);
	SK_API void CreateCMakeProject(StringView directory);
	SK_API void OpenProjectInEditor(StringView projectPath);
}
