#include "RmlUiManager.hpp"

#include "Skore/Core/Array.hpp"

namespace Skore
{
	namespace
	{
		RenderInterfaceSkore* gRenderInterface = nullptr;
		Array<Rml::Context*>  gContexts;
	}

	void RmlUiManager::SetRenderInterface(RenderInterfaceSkore* renderInterface)
	{
		gRenderInterface = renderInterface;
	}

	RenderInterfaceSkore* RmlUiManager::GetRenderInterface()
	{
		return gRenderInterface;
	}

	void RmlUiManager::RegisterContext(Rml::Context* context)
	{
		if (context && gContexts.IndexOf(context) == nPos)
		{
			gContexts.EmplaceBack(context);
		}
	}

	void RmlUiManager::UnregisterContext(Rml::Context* context)
	{
		gContexts.Remove(context);
	}

	Span<Rml::Context*> RmlUiManager::GetContexts()
	{
		return gContexts;
	}

	SK_API void RmlUiTestInit(); // temporary RmlUi verification harness (RmlUiTest.cpp)

	// Interface-free by design: Rml::Initialise() and the render/system/file interfaces are created
	// and owned by UIComponent. RmlUIInit/Shutdown remain as App lifecycle hooks only.
	void RmlUIInit()
	{
		// TEMP: spawn a test context so RmlUi rendering can be verified in the editor/player.
		// Remove this call once UIComponent drives context creation.
		RmlUiTestInit();
	}

	void RmlUIShutdown() {}
}
