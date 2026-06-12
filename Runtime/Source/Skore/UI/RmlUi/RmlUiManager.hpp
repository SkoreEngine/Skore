#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Span.hpp"

namespace Rml
{
	class Context;
}

namespace Skore
{
	class RenderInterfaceSkore;

	// Central registry for RmlUi. The render/system/file interfaces and the Rml::Contexts are
	// created and owned by UIComponent(s); the manager only tracks them so RmlUiRenderPass knows
	// which contexts to draw and which render interface to drive. Single-threaded: contexts are
	// (un)registered during scene update and read during render, both on the main thread.
	struct SK_API RmlUiManager
	{
		static void                  SetRenderInterface(RenderInterfaceSkore* renderInterface);
		static RenderInterfaceSkore* GetRenderInterface();

		static void                RegisterContext(Rml::Context* context);
		static void                UnregisterContext(Rml::Context* context);
		static Span<Rml::Context*> GetContexts();
	};
}
