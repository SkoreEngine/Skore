#include "Skore/RegisterTypes.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterCoreTypes();
	void RegisterResourceTypes();
	void RegisterAppTypes();
	void RegisterIOTypes();
	void RegisterSceneTypes();
	void RegisterUITypes();
	void RegisterGraphicsTypes();
	void RegisterAudioTypes();
	void RegisterOpenXRTypes();
	void RegisterNavigationTypes();
	void RegisterScriptTypes();

	void RegisterTypes()
	{
		{
			GroupScope scope("Core");
			RegisterCoreTypes();
		}

		{
			GroupScope scope("Resources");
			RegisterResourceTypes();
		}

		{
			GroupScope scope("App");
			RegisterAppTypes();
		}

		{
			GroupScope scope("IO");
			RegisterIOTypes();
		}

		{
			GroupScope scope("Graphics");
			RegisterGraphicsTypes();
		}
		{
			GroupScope scope("Audio");
			RegisterAudioTypes();
		}

		{
			GroupScope scope("World");
			RegisterSceneTypes();
		}

		{
			GroupScope scope("UI");
			RegisterUITypes();
		}

		{
			GroupScope scope("XR");
			RegisterOpenXRTypes();
		}

		{
			GroupScope scope("Navigation");
			RegisterNavigationTypes();
		}

		{
			GroupScope scope("Script");
			RegisterScriptTypes();
		}

		Reflection::Type<App>();
	}
}