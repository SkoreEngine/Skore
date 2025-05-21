// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "ProjectManager.hpp"

#include <mutex>

#include "imgui.h"
#include "SDL3/SDL_dialog.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_misc.h"
#include "Skore/App.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StaticContent.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{
#define RECENT_PROJECTS 10
#define NEW_PROJECTS 11

	SDL_Window* GraphicsGetWindow();

	class ProjectManagerUserData : public Object
	{
	public:
		SK_CLASS(ProjectManagerUserData, Object)

		Array<String> recentProjects;
		String        recentProjectDirectory;
	};

	namespace
	{
		GPUTexture* logoTexture;
		GPUTexture* emptyProject;
		String      projectSearch;
		String      newProjectName;
		String      newProjectPath;
		String      settingsFilePath;
		String      selectedProject;
		String      projectToOpen;
		std::mutex  projectManagerMutex;

		u32  selectedWindow = RECENT_PROJECTS;
		u32  templateSelected = 1;
		bool createProject = false;

		bool focus = false;

		ProjectManagerUserData projectManagerUserData;
	}

	void EditorInit(StringView project);

	void RegisterProjectManagerTypes()
	{
		auto projectManagerUserData = Reflection::Type<ProjectManagerUserData>();
		projectManagerUserData.Field<&ProjectManagerUserData::recentProjects>("recentProjects");
		projectManagerUserData.Field<&ProjectManagerUserData::recentProjectDirectory>("recentProjectDirectory");
	}

	void ProjectManager::Init()
	{
		Event::Bind<OnUpdate, &ProjectManager::Update>();
		Event::Bind<OnShutdown, &ProjectManager::Shutdown>();

		logoTexture = StaticContent::GetTexture("Content/Images/LogoSmall.jpeg");
		emptyProject = StaticContent::GetTexture("Content/Images/minimalist-logo.png");

		char* skoreFolder = SDL_GetPrefPath(nullptr, "Skore");
		settingsFilePath = Path::Join(skoreFolder, "ProjectManager.cfg");
		SDL_free(skoreFolder);

		LoadDataFile();

		if (projectManagerUserData.recentProjectDirectory.Empty())
		{
			newProjectPath = Path::Join(SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS), "Skore Projects");
		}
		else
		{
			newProjectPath = projectManagerUserData.recentProjectDirectory;
		}
	}

	void ProjectManager::RequestShutdown()
	{
		Event::Unbind<OnShutdown, &ProjectManager::Shutdown>();
		Shutdown();
	}

	void ProjectManager::Shutdown()
	{
		Graphics::WaitIdle();
		logoTexture->Destroy();
		emptyProject->Destroy();
		Event::Unbind<OnUpdate, &ProjectManager::Update>();
	}

	void ProjectManager::DialogChooseDirectory(void* userdata, const char* const* filelist, int filter)
	{
		newProjectPath = filelist[0];
	}

	void ProjectManager::DialogOpenProject(void* userdata, const char* const* filelist, int filter)
	{
		if (filelist != nullptr)
		{
			String projectFile = filelist[0];
			if (!projectFile.Empty())
			{
				std::lock_guard lock(projectManagerMutex);
				projectToOpen = projectFile;
			}
		}
	}

	void ProjectManager::CreateProject(StringView location, StringView projectName, u32 templateId)
	{
		String projectPath = Path::Join(location, projectName);
		String projectFile = Path::Join(projectPath, String(projectName) + SK_PROJECT_EXTENSION);
		String libraryPath = Path::Join(projectPath, "Library");
		String assetsPath = Path::Join(projectPath, "Assets");

		FileSystem::CreateDirectory(projectPath);
		FileSystem::CreateDirectory(libraryPath);
		FileSystem::CreateDirectory(assetsPath);

		if (templateId == 2)
		{	}

		FileSystem::SaveFileAsString(projectFile, "//TODO: Create project file");

		projectManagerUserData.recentProjects.EmplaceBack(projectFile);
		projectManagerUserData.recentProjectDirectory = location;
		SaveDataFile();
		RequestShutdown();

		SDL_MaximizeWindow(GraphicsGetWindow());

		App::RunOnMainThread([projectFile]
		{
			EditorInit(projectFile);
		});
	}

	void ProjectManager::OpenProject(StringView projectFile)
	{
		if (projectFile.Empty()) return;

		bool projectFound = false;
		for (StringView str : projectManagerUserData.recentProjects)
		{
			if (str == projectFile)
			{
				projectFound = true;
				break;
			}
		}

		if (!projectFound)
		{
			projectManagerUserData.recentProjects.EmplaceBack(projectFile);
			SaveDataFile();
		}

		RequestShutdown();

		SDL_MaximizeWindow(GraphicsGetWindow());

		App::RunOnMainThread([projectFile]
		{
			EditorInit(projectFile);
		});
	}

	void ProjectManager::LoadDataFile()
	{
		YamlArchiveReader reader(FileSystem::ReadFileAsString(settingsFilePath));
		projectManagerUserData.Deserialize(reader);

		bool hasErased = false;
		auto it = projectManagerUserData.recentProjects.begin();
		while (it != projectManagerUserData.recentProjects.end())
		{
			if (!FileSystem::GetFileStatus(*it).exists)
			{
				it = projectManagerUserData.recentProjects.Erase(it);
				hasErased = true;
			}
			else
			{
				++it;
			}
		}
		if (hasErased)
		{
			SaveDataFile();
		}
	}

	void ProjectManager::SaveDataFile()
	{
		YamlArchiveWriter writer;
		projectManagerUserData.Serialize(writer);
		FileSystem::SaveFileAsString(settingsFilePath, writer.EmitAsString());
	}

	void ProjectManager::Update()
	{
		{
			std::lock_guard lock(projectManagerMutex);
			if (!projectToOpen.Empty())
			{
				OpenProject(projectToOpen);
				return;
			}
		}

		if (createProject)
		{
			CreateProject(newProjectPath, newProjectName, templateSelected);
			return;
		}


		auto& style = ImGui::GetStyle();
		auto  padding = style.WindowPadding;

		ScopedStyleVar itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		ImGuiBeginFullscreen(5000);

		auto listOptionsPanelSize = ImGui::GetContentRegionAvail().x * 0.2f;
		auto availableSpace = ImGui::GetContentRegionAvail().x - listOptionsPanelSize;

		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.1f, 0.5f));
		if (ImGui::BeginChild(52010, ImVec2(listOptionsPanelSize, 0)))
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding.y);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding.y);

			ImGuiTextureItem(logoTexture, ImVec2(48 * ImGui::GetStyle().ScaleFactor, 48 * ImGui::GetStyle().ScaleFactor));

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding.y);

			ImGui::Separator();

			auto buttonSize = ImVec2(listOptionsPanelSize, 35 * style.ScaleFactor);

			if (ImGui::Selectable(ICON_FA_DIAGRAM_PROJECT " Recent Projects", selectedWindow == RECENT_PROJECTS, ImGuiSelectableFlags_SpanAllColumns, buttonSize))
			{
				selectedWindow = RECENT_PROJECTS;
			}

			if (ImGui::Selectable(ICON_FA_PLUS " New Project", selectedWindow == NEW_PROJECTS, ImGuiSelectableFlags_SpanAllColumns, buttonSize))
			{
				selectedWindow = NEW_PROJECTS;
				focus = false;
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(); //ImGuiStyleVar_SelectableTextAlign

		ImGui::SameLine();

		if (selectedWindow == RECENT_PROJECTS)
		{
			ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
			ScopedStyleColor frameBg(ImGuiCol_FrameBg, IM_COL32(22, 23, 25, 255));
			ScopedStyleVar   frameBorderSize(ImGuiStyleVar_FrameBorderSize, 0.f);

			if (ImGui::BeginChild(52020, ImVec2(0, 0)))
			{
				auto buttonSize = ImVec2(100 * ImGui::GetStyle().ScaleFactor, 25 * ImGui::GetStyle().ScaleFactor);
				auto width = ImGui::GetContentRegionAvail().x - buttonSize.x - (25 * ImGui::GetStyle().ScaleFactor);

				ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + padding.x, ImGui::GetCursorPos().y + padding.y));

				ImGui::SetNextItemWidth(width);

				if (ImGuiSearchInputText(80005, projectSearch))
				{
					//searchText = ToUpper(projectSearch);
				}

				ImGui::SameLine();

				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding.x);
				if (ImGui::Button("Open", buttonSize))
				{
					SDL_DialogFileFilter filter{};
					filter.name = "Skore Project";
					filter.pattern = "skore";
					SDL_ShowOpenFileDialog(DialogOpenProject, nullptr, GraphicsGetWindow(), &filter, 1, newProjectPath.CStr(), false);
				}


				ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + padding.x, ImGui::GetCursorPos().y + padding.y));
				ImGui::Separator();
				ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + padding.x * 1.5f, ImGui::GetCursorPos().y + padding.y * 1.5f));
				bool openPopup = false;
				{
					ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.01f, 0.5f));

					if (ImGuiBeginContentTable("asset-selection", 1.0))
					{
						for (String& recentProject : projectManagerUserData.recentProjects)
						{
							String projectName = Path::Name(recentProject);

							ImGuiContentItemDesc contentItem{};
							contentItem.id = HashValue(projectName);
							contentItem.label = projectName;
							contentItem.thumbnailScale = 1.0;
							contentItem.texture = logoTexture;

							ImGuiContentItemState state = ImGuiContentItem(contentItem);

							if (state.enter)
							{
								projectToOpen = recentProject;
							}

							if (state.clicked)
							{
								selectedProject = recentProject;
							}

							if (state.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
							{
								openPopup = true;

							}
						}
						ImGuiEndContentTable();
					}
					ImGui::PopStyleVar(); //ImGuiStyleVar_SelectableTextAlign
				}

				if (openPopup)
				{
					ImGui::OpenPopup("project-browser-popup");
				}

				auto popupRes = ImGuiBeginPopupMenu("project-browser-popup");
				if (popupRes)
				{
					if (ImGui::MenuItem(ICON_FA_FOLDER " Show in Explorer"))
					{
						SDL_OpenURL(Path::Parent(selectedProject).CStr());
					}

					if (ImGui::MenuItem(ICON_FA_TRASH " Remove"))
					{
						if (auto it = FindFirst(projectManagerUserData.recentProjects.begin(), projectManagerUserData.recentProjects.end(), selectedProject))
						{
							projectManagerUserData.recentProjects.Erase(it);
							SaveDataFile();
						}
					}
				}
				ImGuiEndPopupMenu(popupRes);

				ImGui::EndChild();
			}
		}
		else if (selectedWindow == NEW_PROJECTS)
		{
			if (ImGui::BeginChild(52150, ImVec2(0, 0)))
			{
				{
					ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
					ScopedStyleColor frameBg(ImGuiCol_FrameBg, IM_COL32(22, 23, 25, 255));
					ScopedStyleVar   frameBorderSize(ImGuiStyleVar_FrameBorderSize, 0.f);

					if (ImGui::BeginChild(52030, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 50 * ImGui::GetStyle().ScaleFactor)))
					{
						ImGui::Separator();

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding.y * 2);
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding.x * 2);

						ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.01f, 0.5f));

						if (ImGuiBeginContentTable("templates", 1.0))
						{
							{
								ImGuiContentItemDesc contentItem{};
								contentItem.id = 879457894;
								contentItem.label = "Default Project";
								contentItem.thumbnailScale = 1.0;
								contentItem.texture = emptyProject;
								contentItem.selected = templateSelected == 1;

								ImGuiContentItemState state = ImGuiContentItem(contentItem);

								if (state.clicked)
								{
									templateSelected = 1;
								}
							}

							{
								ImGuiContentItemDesc contentItem{};
								contentItem.id = 879457895;
								contentItem.label = "C++ Project";
								contentItem.thumbnailScale = 1.0;
								contentItem.texture = emptyProject;
								contentItem.selected = templateSelected == 2;

								ImGuiContentItemState state = ImGuiContentItem(contentItem);

								if (state.clicked)
								{
									templateSelected = 2;
								}
							}

							ImGuiEndContentTable();
						}
						ImGui::PopStyleVar(); //ImGuiStyleVar_SelectableTextAlign
					}
					ImGui::EndChild();
				}

				{
					ScopedStyleVar childPadding(ImGuiStyleVar_WindowPadding, padding);

					ImGui::Separator();

					if (ImGui::BeginChild(52040, ImVec2(0.0, 0.0), false, ImGuiWindowFlags_AlwaysUseWindowPadding))
					{
						static bool validation = false;

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding.y);

						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted("Project Name: ");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(150 * ImGui::GetStyle().ScaleFactor);
						if (!focus)
						{
							ImGui::SetKeyboardFocusHere();
							focus = true;
						}

						if (ImGuiInputText(678347, newProjectName, 0, validation ? ImGuiInputTextExtraFlags_ShowError : 0))
						{
							validation = false;
						}

						ImGui::SameLine();
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding.x);
						ImGui::TextUnformatted("Location: ");
						ImGui::SameLine();

						String currentPath = Path::Join(newProjectPath, newProjectName);

						ImGui::SetNextItemWidth(Math::Max(ImGui::GetContentRegionAvail().x - 250, 200 * ImGui::GetStyle().ScaleFactor));
						ImGui::BeginDisabled(true);
						ImGuiInputTextReadOnly(678348, currentPath);
						ImGui::EndDisabled();
						ImGui::SameLine();
						if (ImGui::Button("..."))
						{
							SDL_ShowOpenFolderDialog(DialogChooseDirectory, nullptr, GraphicsGetWindow(), newProjectPath.CStr(), false);
						}
						ImGui::SameLine();
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding.x);


						if (ImGui::Button("Create Project", ImVec2(130 * ImGui::GetStyle().ScaleFactor, 25 * ImGui::GetStyle().ScaleFactor)))
						{
							if (newProjectName.Empty())
							{
								ImGui::OpenPopup("Please provide a project name");
								validation = true;
							}
							else if (FileSystem::GetFileStatus(currentPath).exists)
							{
								validation = true;
								ImGui::OpenPopup("Project already exists");
							}

							if (!validation)
							{
								createProject = true;
							}
						}

						if (ImGui::BeginPopupModal("Please provide a project name"))
						{
							ImGui::Text("Please enter a project name. This field is required to create the project.");

							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 50 * ImGui::GetStyle().ScaleFactor);

							if (ImGui::Button("Close"))
							{
								ImGui::CloseCurrentPopup();
							}
							ImGui::EndPopup();
						}

						if (ImGui::BeginPopupModal("Project already exists"))
						{
							ImGui::Text("A project with this name already exists. Please choose a different name for your project.");

							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 50 * ImGui::GetStyle().ScaleFactor);

							if (ImGui::Button("Close"))
							{
								ImGui::CloseCurrentPopup();
							}
							ImGui::EndPopup();
						}

						//ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Project name cannot be empty");
					}
					ImGui::EndChild();
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
