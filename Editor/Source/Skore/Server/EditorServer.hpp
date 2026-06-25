#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	struct HttpServerSettings
	{
		enum
		{
			Enabled,
			Port,
			Host
		};
	};

	struct SK_API EditorServer
	{
		static void Init();
		static void Update();
		static void Shutdown();

		static bool       IsRunning();
		static u16        GetPort();
		static StringView GetHost();
	};

	void RegisterEditorServerTypes();
}
