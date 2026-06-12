#include "RmlUiManager.hpp"

#include "RenderInterfaceSkore.hpp"
#include "SystemInterfaceSkore.hpp"
#include "FontEngineSkore.hpp"
#include "FileInterfaceSkore.hpp"

#include "Skore/Core/Allocator.hpp"

#include <RmlUi/Core.h>

namespace Skore
{
	namespace
	{
		RenderInterfaceSkore* renderInterface = nullptr;
		SystemInterfaceSkore* systemInterface = nullptr;
		FontEngineSkore*      fontEngine = nullptr;
		FileInterfaceSkore*   fileInterface = nullptr;
	}

	RenderInterfaceSkore* RmlUiManager::GetRenderInterface()
	{
		return renderInterface;
	}

	void RmlUIInit()
	{
		renderInterface = Alloc<RenderInterfaceSkore>();
		systemInterface = Alloc<SystemInterfaceSkore>();
		fontEngine = Alloc<FontEngineSkore>();
		fileInterface = Alloc<FileInterfaceSkore>();

		Rml::SetRenderInterface(renderInterface);
		Rml::SetSystemInterface(systemInterface);
		Rml::SetFontEngineInterface(fontEngine);
		Rml::SetFileInterface(fileInterface);
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
		if (fileInterface)
		{
			DestroyAndFree(fileInterface);
			fileInterface = nullptr;
		}
	}
}
