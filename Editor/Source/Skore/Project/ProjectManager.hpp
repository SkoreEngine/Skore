#pragma once
#include "Skore/Core/StringView.hpp"


namespace Skore
{
	class SK_API ProjectManager
	{
	public:
		static void Init();

		static void RequestShutdown();

	private:
		void Update();
		static void Shutdown();
		static void CreateProject(StringView location, StringView projectName, u32 templateId);
		static void OpenProject(StringView projectFile);
		static void LoadDataFile();
		static void SaveDataFile();
	};
}
