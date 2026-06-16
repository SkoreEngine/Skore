#pragma once

#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	struct SK_API HostfxrResolver
	{
		static bool TryGetPathFromDotnetRoot(StringView dotnetRoot, String& fxrPath);
		static bool TryGetPath(String& dotnetRoot, String& fxrPath);
	};
}
