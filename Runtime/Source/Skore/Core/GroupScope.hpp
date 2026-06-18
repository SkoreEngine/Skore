#pragma once

#include "String.hpp"
#include "StringView.hpp"

namespace Skore
{
	SK_API void   PushGroup(StringView name);
	SK_API void   PopGroup();
	SK_API String GetCurrentScope();

	struct GroupScope
	{
		GroupScope(StringView name)
		{
			PushGroup(name);
		}

		~GroupScope()
		{
			PopGroup();
		}
	};
}
