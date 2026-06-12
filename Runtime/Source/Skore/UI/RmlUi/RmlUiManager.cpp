#include "RmlUiManager.hpp"

#include "RenderInterfaceSkore.hpp"
#include "SystemInterfaceSkore.hpp"
#include "FontEngineSkore.hpp"

#include "Skore/Core/Array.hpp"
#include "Skore/Core/Allocator.hpp"

#include <RmlUi/Core.h>

namespace Skore
{
	namespace
	{
		RenderInterfaceSkore* renderInterface = nullptr;
		SystemInterfaceSkore* systemInterface = nullptr;
		FontEngineSkore*      fontEngine = nullptr;
		Array<Rml::Context*>  contexts;
	}

	RenderInterfaceSkore* RmlUiManager::GetRenderInterface()
	{
		return renderInterface;
	}

	void RmlUiManager::RegisterContext(Rml::Context* context)
	{
		if (context && contexts.IndexOf(context) == nPos)
		{
			contexts.EmplaceBack(context);
		}
	}

	void RmlUiManager::UnregisterContext(Rml::Context* context)
	{
		contexts.Remove(context);
	}

	Span<Rml::Context*> RmlUiManager::GetContexts()
	{
		return contexts;
	}

	void RmlUIInit()
	{
		renderInterface = Alloc<RenderInterfaceSkore>();
		systemInterface = Alloc<SystemInterfaceSkore>();
		fontEngine = Alloc<FontEngineSkore>();

		Rml::SetRenderInterface(renderInterface);
		Rml::SetSystemInterface(systemInterface);
		Rml::SetFontEngineInterface(fontEngine);
		Rml::Initialise();
	}

	void RmlUIShutdown()
	{
		Rml::Shutdown();

		if (renderInterface)
		{
			renderInterface->Destroy();
			DestroyAndFree(renderInterface);
			renderInterface = nullptr;
		}
		if (systemInterface)
		{
			DestroyAndFree(systemInterface);
			systemInterface = nullptr;
		}
		if (fontEngine)
		{
			DestroyAndFree(fontEngine);
			fontEngine = nullptr;
		}
		contexts.Clear();
	}
}
