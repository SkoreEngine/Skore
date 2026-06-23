#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	struct GeneralEditorSettings
	{
		enum
		{
			LoadPreviousProjectOnStartup
		};
	};

	SK_API String GetEditorSettingsPath();
	SK_API RID    LoadEditorSettingsResource();
	SK_API void   SaveEditorSettingsResource();
	SK_API bool   ShouldLoadPreviousProjectOnStartup();
	void          RegisterEditorSettingsTypes();
}
