#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	struct SK_API CSharpBindingGenerator
	{
		static void Generate(StringView outputDir);
	};
}
