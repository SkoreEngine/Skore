#pragma once
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"


namespace Skore
{
	enum class ProjectManagerTab
	{
		RecentProjects,
		NewProject
	};

	class SK_API ProjectManager
	{
	public:
		static void Init(ProjectManagerTab initialTab = ProjectManagerTab::RecentProjects);
		static Array<String> GetRecentProjects();
		static bool LaunchProject(StringView projectFile);
		static bool LaunchProjectManager(ProjectManagerTab initialTab = ProjectManagerTab::RecentProjects);
		static void OpenProject(StringView projectFile);
		static String LoadLastOpenedProject();
		static void SaveLastOpenedProject(StringView projectFile);
	private:
		void Update();
		static void Shutdown();
		static void CreateProject(StringView location, StringView projectName, u32 templateId);
		static void LoadDataFile();
		static void SaveDataFile();
	};
}
