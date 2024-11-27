#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
	template<typename Type, typename Enable = void>
	struct TypeApiInfo
	{
		static void ExtractApi(VoidPtr pointer)
		{

		}

		static constexpr TypeID GetApiId()
		{
			return 0;
		}
	};
}