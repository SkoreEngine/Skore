#include <RmlUi/Core.h>

#include "RenderInterfaceSkore.hpp"
#include "SystemInterfaceSkore.hpp"


namespace Skore
{
	static RenderInterfaceSkore renderInterface;
	static SystemInterfaceSkore systemInterface;

	void RmlUIInit()
	{
    Rml::SetRenderInterface(&renderInterface);
		Rml::SetSystemInterface(&systemInterface);

		Rml::Initialise();
	}

	void RmlUIShutdown()
	{
		Rml::Shutdown();
	}


}
