#pragma once

#include "Skore/Common.hpp"

namespace Rml
{
	class Context;
}

namespace Skore
{
	class RenderInterfaceSkore;

	struct SK_API RmlUiManager
	{
		static RenderInterfaceSkore* GetRenderInterface();
		static void                  RegisterContext(Rml::Context* context);
		static void                  UnregisterContext(Rml::Context* context);
	};
}
