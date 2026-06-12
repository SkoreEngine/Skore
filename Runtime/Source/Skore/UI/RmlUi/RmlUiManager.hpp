#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
	class RenderInterfaceSkore;

	struct SK_API RmlUiManager
	{
		static RenderInterfaceSkore* GetRenderInterface();
	};
}
