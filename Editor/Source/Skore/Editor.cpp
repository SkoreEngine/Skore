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

#include "Editor.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Window/ProjectBrowserWindow.hpp"

#include "EditorWorkspace.hpp"
#include "Resource/ResourceAssets.hpp"
#include "SDL3/SDL_loadso.h"
#include "SDL3/SDL_process.h"
#include "Skore/Events.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Utils/ProjectUtils.hpp"
#include "Window/ConsoleWindow.hpp"
#include "Window/EntityTreeWindow.hpp"
#include "Window/HistoryWindow.hpp"
#include "Window/PropertiesWindow.hpp"
#include "Window/SceneViewWindow.hpp"
#include "Window/TextureViewWindow.hpp"

#include <thread>
#include <chrono>

#include "Skore/Core/Settings.hpp"
#include "Skore/IO/Input.hpp"
#include "Utils/StaticContent.hpp"
#include "Window/SettingsWindow.hpp"

namespace Skore
{
	void ShaderManagerInit();
	void ShaderManagerShutdown();

	namespace
	{

		enum class DialogModalType
		{
			Confirmation,
			Error
		};

		struct DialogModalData
		{
			String            message;
			VoidPtr           userData;
			FnConfirmCallback callback;
			DialogModalType   type;
		};

		struct EditorWindowStorage
		{
			TypeID       typeId{};
			DockPosition dockPosition{};
			bool         createOnInit{};
			i32          order;
		};

		struct OpenWindowStorage
		{
			u32           id{};
			EditorWindow* instance{};
			ReflectType*  reflectType{};
		};

		struct UndoRedoScopeStorage
		{
			UndoRedoScope* scope;
			SK_NO_COPY_CONSTRUCTOR(UndoRedoScopeStorage);

			UndoRedoScopeStorage(UndoRedoScope* scope) : scope(scope) {}

			UndoRedoScopeStorage(UndoRedoScopeStorage& other) : scope(other.scope)
			{
				other.scope = nullptr;
			}

			~UndoRedoScopeStorage()
			{
				if (scope != nullptr)
				{
					Resources::DestroyScope(scope);
				}
			}
		};

		Array<EditorWindowStorage> editorWindowStorages;
		Array<OpenWindowStorage>   openWindows;
		Queue<DialogModalData>     dialogModals;
		ConsoleSink                consoleSink;

		RID                     projectRID;
		String                  projectPath;
		String                  projectAssetPath;
		String                  projectTempPath;
		String                  projectPackagePath;
		Array<UpdatedAssetInfo> updatedItems;

		String                  projectSettingsPath;
		RID						projectSettingsRID;
		u64						projectSettingsLastPersistedVersion = 0;

		//c++ dev plugin
		String                   pluginProjectPath;
		u64                      pluginLastModifiedTime = 0;

		Array<RID>    packages;
		Array<String> packagePaths;

		bool forceClose = false;
		bool shouldOpenPopup = false;

		std::unique_ptr<EditorWorkspace> workspace;

		Array<std::unique_ptr<UndoRedoScopeStorage>> undoActions{};
		Array<std::unique_ptr<UndoRedoScopeStorage>> redoActions{};
		bool undoRedoLocked = false;

		bool debugOptionsEnabled = false;

		std::mutex funcsMutex;
		Queue<std::function<void()>> funcs;

		MenuItemContext menuContext{};
		bool            dockInitialized = false;
		u32             dockSpaceId{10000};
		u32             centerSpaceId{10000};
		u32             rightTopDockId{};
		u32             rightBottomDockId{};
		u32             bottomLeftDockId{};
		u32             bottomRightDockId{};
		u32             leftDockId{};
		u32             idCounter{100000};
		bool            showImGuiDemo = false;


		Logger& logger = Logger::GetLogger("Skore::Editor");

		void ShowImGuiDemo(const MenuItemEventData& eventData)
		{
			showImGuiDemo = true;
		}

		void GetUpdatedItems()
		{
			updatedItems.Clear();
			ResourceAssets::GetUpdatedAssets(projectRID, updatedItems);

			for (RID package: packages)
			{
				ResourceAssets::GetUpdatedAssets(package, updatedItems);
			}
		}

		void Save()
		{
			ResourceAssets::SaveAssetsToDirectory(projectAssetPath, projectRID, updatedItems);
		}

		void SaveAll(const MenuItemEventData& eventData)
		{
			GetUpdatedItems();
			Save();
		}

		void CloseEngine(const MenuItemEventData& eventData)
		{
			App::RequestShutdown();
		}

		void Undo(const MenuItemEventData& eventData)
		{
			std::unique_ptr<UndoRedoScopeStorage> action = Traits::Move(undoActions.Back());
			Resources::Undo(action->scope);
			redoActions.EmplaceBack(Traits::Move(action));
			undoActions.PopBack();
		}

		bool UndoEnabled(const MenuItemEventData& eventData)
		{
			return !undoRedoLocked && !undoActions.Empty();
		}

		void Redo(const MenuItemEventData& eventData)
		{
			std::unique_ptr<UndoRedoScopeStorage> action = Traits::Move(redoActions.Back());
			Resources::Redo(action->scope);
			redoActions.PopBack();
			undoActions.EmplaceBack(Traits::Move(action));
		}

		bool RedoEnabled(const MenuItemEventData& eventData)
		{
			return !undoRedoLocked && !redoActions.Empty();
		}

		bool HasEntitySelection(const MenuItemEventData& eventData)
		{
			return Editor::GetCurrentWorkspace().GetSceneEditor()->HasSelectedEntities();
		}

		void Duplicate(const MenuItemEventData& eventData)
		{
			Editor::GetCurrentWorkspace().GetSceneEditor()->DuplicateSelected();
		}

		void Delete(const MenuItemEventData& eventData)
		{
			Editor::GetCurrentWorkspace().GetSceneEditor()->DestroySelected();
		}

		bool CanOpenEditor(const MenuItemEventData& eventData)
		{
			return HasCMakeProject(projectPath);
		}

		bool CreateCMakeProjectVisible(const MenuItemEventData& eventData)
		{
			return !HasCMakeProject(projectPath);
		}

		void CreateCMakeProject(const MenuItemEventData& eventData)
		{
			CreateCMakeProject(projectPath);
		}


		void ExportProject(bool run)
		{
			String exportPath = Path::Join(projectPath, "Export");
			if (FileSystem::GetFileStatus(exportPath).exists)
			{
				if (!FileSystem::Remove(exportPath))
				{
					return;
				}
			}
			FileSystem::CreateDirectory(exportPath);

			//TODO - manage by installations.
			String cur = FileSystem::CurrentDir();
			for (const String& file: DirectoryEntries{cur})
			{
				String extention = Path::Extension(file);
				if (extention != SK_EXEC_EXT && extention != SK_SHARED_EXT)
				{
					continue;
				}
				String name = Path::Name(file);
				if (name == "SkoreTests")
				{
					continue;
				}

				if (name == "SkoreRuntime" || name == "SDL3")
				{
					FileSystem::CopyFile(file, Path::Join(exportPath, Path::Name(file) + SK_SHARED_EXT));
				}

				if (Path::Name(file) == "SkorePlayer")
				{
					FileSystem::CopyFile(file, Path::Join(exportPath, Path::Name(projectPath) + Path::Extension(file)));
				}
			}

			//TODO copy plugins

			if (FileSystem::GetFileStatus(pluginProjectPath).exists)
			{
				String pluginsPath = Path::Join(exportPath, "Plugins");
				FileSystem::CreateDirectory(pluginsPath);
				FileSystem::CopyFile(pluginProjectPath, Path::Join(pluginsPath, Path::Name(pluginProjectPath) + SK_SHARED_EXT));
			}

			//export packages
			{
				Array<RID> packagesToExport;
				packagesToExport.Reserve(packages.Size() + 1);
				for (RID package : packages)
				{
					packagesToExport.EmplaceBack(package);
				}
				packagesToExport.EmplaceBack(projectRID);

				BinaryArchiveWriter writer;
				writer.BeginSeq("assets");
				ResourceAssets::ExportPackages(packagesToExport, writer);
				writer.EndSeq();

				FileSystem::SaveFileAsByteArray(Path::Join(Path::Join(projectPath, "Export"), Path::Name(projectPath) + ".pak"), writer.GetData());
			}
			//export settings
			{
				BinaryArchiveWriter writer;
				writer.BeginMap("projectSettings");
				Settings::Save(writer, TypeInfo<ProjectSettings>::ID());
				writer.EndMap();
				FileSystem::SaveFileAsByteArray(Path::Join(Path::Join(projectPath, "Export"), "Engine.bcfg"), writer.GetData());
			}

			logger.Debug("Project exported to {}", Path::Join(projectPath, "Export"));

			if (run)
			{
				String command = Path::Join(exportPath, Path::Name(projectPath) + SK_EXEC_EXT);
				const char *args[] = { command.CStr(), "--current-path", exportPath.CStr(), NULL };
				if (SDL_Process* process = SDL_CreateProcess(args, true))
				{
					SDL_DestroyProcess(process);
				}
			}
		}

		void Export(const MenuItemEventData& eventData)
		{
			ExportProject(false);
		}

		void ExportAndRun(const MenuItemEventData& eventData)
		{
			ExportProject(true);
		}

		void ReloadShaders(const MenuItemEventData& eventData)
		{
			// for (AssetFile* assetFile : AssetEditor::GetAssetsOfType(GetTypeID<ShaderAsset>()))
			// {
			//     if (ShaderAsset* shaderAsset = Assets::Get<ShaderAsset>(assetFile->uuid))
			//     {
			//         if (!shaderAsset->states.Empty())
			//         {
			//             Assets::Reload(assetFile->uuid);
			//         }
			//     }
			// }
		}

		void ShowDebugOptions(const MenuItemEventData& eventData)
		{
			debugOptionsEnabled = !debugOptionsEnabled;
		}

		bool IsDebugOptionsEnabled(const MenuItemEventData& eventData)
		{
			return debugOptionsEnabled;
		}

		void OpenProjectInEditorAction(const MenuItemEventData& eventData)
		{
			OpenProjectInEditor(projectPath);
		}

		void CreateMenuItems()
		{
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File", .priority = 0});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/New Scene", .priority = 0, .action = NewProject});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Open Scene", .priority = 10, .action = OpenScene});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Recent Scenes", .priority = 10, .action = OpenScene});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Save All", .priority = 1000, .itemShortcut{.ctrl = true, .presKey = Key::S}, .action = SaveAll});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Build Settings...", .priority = 2000, .action = Build});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Export", .priority = 2000, .action = Export});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Export And Run", .priority = 2005, .action = ExportAndRun});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Exit", .priority = I32_MAX, .itemShortcut{.ctrl = true, .presKey = Key::Q}, .action = CloseEngine});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit", .priority = 30});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Undo", .priority = 10, .itemShortcut{.ctrl = true, .presKey = Key::Z}, .action = Undo, .enable = UndoEnabled});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Redo", .priority = 20, .itemShortcut{.ctrl = true, .shift = true, .presKey = Key::Z}, .action = Redo, .enable = RedoEnabled});

			// Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Duplicate", .priority = 500, .itemShortcut{.ctrl = true, .presKey = Key::D}, .action = Duplicate, .enable = HasEntitySelection});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Delete", .priority = 500, .itemShortcut{.presKey = Key::Delete}, .action = Delete, .enable = HasEntitySelection});

			// Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Editor Preferences...", .priority = 1000, .action = SettingsWindow::Open, .userData = GetTypeID<EditorPreferences>()});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools", .priority = 50});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Open Editor", .priority = 5, .action = OpenProjectInEditorAction, .visible = CanOpenEditor});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Create CMake Project", .priority = 10, .action = CreateCMakeProject, .visible = CreateCMakeProjectVisible});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Reload Shaders", .priority = 100, .itemShortcut = {.presKey = Key::F5}, .action = ReloadShaders});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Show Debug Options", .priority = 105, .action = ShowDebugOptions, .selected = IsDebugOptionsEnabled});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Window", .priority = 60});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Help", .priority = 70});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Dear ImGui Demo", .priority = I32_MAX, .action = ShowImGuiDemo});
		}

		u32 GetDockId(DockPosition dockPosition)
		{
			switch (dockPosition)
			{
				case DockPosition::None: return U32_MAX;
				case DockPosition::Center: return centerSpaceId;
				case DockPosition::Left: return leftDockId;
				case DockPosition::RightTop: return rightTopDockId;
				case DockPosition::RightBottom: return rightBottomDockId;
				case DockPosition::BottomLeft: return bottomLeftDockId;
				case DockPosition::BottomRight: return bottomRightDockId;
			}
			return U32_MAX;
		}

		void Shutdown()
		{
			menuContext = {};

			for (OpenWindowStorage& openWindow : openWindows)
			{
				if (openWindow.instance)
				{
					openWindow.reflectType->Destroy(openWindow.instance);
				}
			}

			workspace = {};

			openWindows.Clear();
			openWindows.ShrinkToFit();
			editorWindowStorages.Clear();
			editorWindowStorages.ShrinkToFit();
			idCounter = 100000;

			undoActions.Clear();
			undoActions.ShrinkToFit();

			redoActions.Clear();
			redoActions.ShrinkToFit();

			ShaderManagerShutdown();
		}

		void DrawMenu()
		{
			ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));
			menuContext.ExecuteHotKeys(nullptr, true);
			if (ImGui::BeginMenuBar())
			{
				menuContext.Draw();
				ImGui::EndMenuBar();
			}
			ImGui::PopStyleColor(1);
		}

		u32 CreateWindow(const EditorWindowStorage& editorWindowStorage, VoidPtr userData)
		{
			ReflectType* reflectType = Reflection::FindTypeById(editorWindowStorage.typeId);
			u32          windowId = idCounter;

			OpenWindowStorage openWindowStorage = OpenWindowStorage{
				.id = windowId,
				.instance = static_cast<EditorWindow*>(reflectType->NewObject()),
				.reflectType = reflectType
			};

			openWindowStorage.instance->Init(openWindowStorage.id, userData);

			openWindows.EmplaceBack(openWindowStorage);
			idCounter = idCounter + 1000;

			auto p = GetDockId(editorWindowStorage.dockPosition);
			if (p != U32_MAX)
			{
				ImGuiDockBuilderDockWindow(windowId, p);
			}

			return windowId;
		}

		void DrawOpenWindows()
		{
			for (u32 i = 0; i < openWindows.Size(); ++i)
			{
				OpenWindowStorage& openWindowStorage = openWindows[i];

				bool open = true;

				openWindowStorage.instance->Draw(openWindowStorage.id, open);
				if (!open)
				{
					openWindowStorage.reflectType->Destroy(openWindowStorage.instance);
					openWindows.Erase(openWindows.begin() + i, openWindows.begin() + i + 1);
				}
			}
		}

		void InitDockSpace()
		{
			if (!dockInitialized)
			{
				dockInitialized = true;
				ImGuiDockBuilderReset(dockSpaceId);

				//create default windows
				centerSpaceId = dockSpaceId;
				rightTopDockId = ImGui::DockBuilderSplitNode(centerSpaceId, ImGuiDir_Right, 0.15f, nullptr, &centerSpaceId);
				rightBottomDockId = ImGui::DockBuilderSplitNode(rightTopDockId, ImGuiDir_Down, 0.50f, nullptr, &rightTopDockId);

				bottomLeftDockId = ImGui::DockBuilderSplitNode(centerSpaceId, ImGuiDir_Down, 0.20f, nullptr, &centerSpaceId);
				bottomRightDockId = ImGui::DockBuilderSplitNode(bottomLeftDockId, ImGuiDir_Right, 0.33f, nullptr, &bottomLeftDockId);

				leftDockId = ImGui::DockBuilderSplitNode(centerSpaceId, ImGuiDir_Left, 0.12f, nullptr, &centerSpaceId);

				for (const auto& windowType : editorWindowStorages)
				{
					if (windowType.createOnInit)
					{
						CreateWindow(windowType, nullptr);
					}
				}
			}
		}

		void ProjectUpdate()
		{
			if (projectSettingsLastPersistedVersion != Resources::GetVersion(projectSettingsRID))
			{
				YamlArchiveWriter writer;
				Settings::Save(writer, TypeInfo<ProjectSettings>::ID());
				FileSystem::SaveFileAsString(projectSettingsPath, writer.EmitAsString());

				projectSettingsLastPersistedVersion = Resources::GetVersion(projectSettingsRID);

				logger.Debug("Project settings saved at {}", projectSettingsPath);
			}


			if (!updatedItems.Empty())
			{
				if (shouldOpenPopup)
				{
					ImGui::OpenPopup("Save Content");
					shouldOpenPopup = false;
				}

				bool                   open{true};
				static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
				ImGuiStyle&            style = ImGui::GetStyle();
				ImGui::SetNextWindowSize({600 * style.ScaleFactor, 400 * style.ScaleFactor}, ImGuiCond_Once);
				ScopedStyleColor childBg(ImGuiCol_PopupBg, IM_COL32(28, 31, 33, 255));
				if (ImGui::BeginPopupModal("Save Content", &open, ImGuiWindowFlags_NoScrollbar))
				{
					ImGui::Text("Pending items to save");
					{
						ScopedStyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));
						ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));

						f32 width = ImGui::GetContentRegionAvail().x - 5;
						f32 height = ImGui::GetContentRegionAvail().y;
						f32 buttonHeight = 25 * style.ScaleFactor;

						if (ImGui::BeginChild(455343, ImVec2(width, height - buttonHeight), false))
						{
							if (ImGui::BeginTable("table-pending-to-save", 4, flags))
							{
								ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None, 30.f * style.ScaleFactor);
								ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 150.f * style.ScaleFactor);
								ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 300.f * style.ScaleFactor);
								ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_None, 100.f * style.ScaleFactor);

								ImGui::TableHeadersRow();

								for (UpdatedAssetInfo& info : updatedItems)
								{
									ImGui::TableNextRow();

									ImGui::TableNextColumn();

									ImGui::BeginHorizontal(info.asset.id);
									ImGui::Spring(1.0);

									ImGui::Checkbox("###", &info.shouldUpdate);

									ImGui::Spring(1.0);
									ImGui::EndHorizontal();

									ImGui::TableNextColumn();
									ImGui::Text("%s", info.displayName.CStr());
									ImGui::TableNextColumn();
									ImGui::Text("%s", info.path.CStr());
									ImGui::TableNextColumn();

									switch (info.type)
									{
										case UpdatedAssetType::Created:
											ImGui::TextColored(ImVec4(0.1, 0.8, 0.1, 1), "Created");
											break;
										case UpdatedAssetType::Updated:
											ImGui::Text("Updated");
											break;
										case UpdatedAssetType::Deleted:
											ImGui::TextColored(ImVec4(0.8, 0.1, 0.1, 1), "Deleted");
											break;
									}
								}
								ImGui::EndTable();
							}

							ImGui::EndChild();
						}

						ImGui::BeginHorizontal("#horizontal-save", ImVec2(width, buttonHeight));

						if (ImGui::Button("Select All"))
						{
							for (UpdatedAssetInfo& info : updatedItems)
							{
								info.shouldUpdate = true;
							}
						}

						if (ImGui::Button("Unselect All"))
						{
							for (UpdatedAssetInfo& info : updatedItems)
							{
								info.shouldUpdate = false;
							}
						}

						ImGui::Spring(1.0f);

						if (ImGui::Button("Save Selected"))
						{
							Save();
							forceClose = true;
							App::RequestShutdown();
						}

						if (ImGui::Button("Don't Save"))
						{
							forceClose = true;
							App::RequestShutdown();
						}

						if (ImGui::Button("Cancel"))
						{
							ImGui::CloseCurrentPopup();
						}

						ImGui::EndHorizontal();
					}
					ImGui::EndPopup();
				}
				else if (!updatedItems.Empty())
				{
					updatedItems.Clear();
				}
			}
		}

		void DrawConfirmDialogs()
		{
			if (!dialogModals.IsEmpty())
			{
				const DialogModalData& modal = dialogModals.Peek();

				const char* title = "";
				switch (modal.type)
				{
					case DialogModalType::Confirmation: title = "Confirmation"; break;
					case DialogModalType::Error: title = "Error"; break;
					// case DialogModalType::Warning: title = "Warning"; break;
					// case DialogModalType::Info: title = "Info"; break;
					// case DialogModalType::Question: title = "Question"; break;
				}


				ImGui::OpenPopup(title);

				ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				{
					ImGui::Text(modal.message.CStr());
					ImGui::Separator();

					if (modal.callback)
					{
						if (ImGui::Button("OK", ImVec2(120, 0)))
						{
							ImGui::CloseCurrentPopup();
							if (modal.callback)
							{
								modal.callback(modal.userData);
							}
							dialogModals.Dequeue();
						}
					}

					ImGui::SetItemDefaultFocus();

					ImGui::SameLine();
					if (ImGui::Button("Close", ImVec2(120, 0)))
					{
						ImGui::CloseCurrentPopup();
						dialogModals.Dequeue();
					}
					ImGui::EndPopup();
				}
			}
		}

		std::chrono::time_point lastExecution = std::chrono::steady_clock::now();

		void LoadProjectPlugin()
		{
			constexpr auto interval = std::chrono::milliseconds(500);
			const auto now = std::chrono::steady_clock::now();
			if (now - lastExecution < interval) {
				return;
			}
			lastExecution = now;

			FileStatus fileStatus =FileSystem::GetFileStatus(pluginProjectPath);
			if (fileStatus.exists)
			{
				if (fileStatus.lastModifiedTime != pluginLastModifiedTime)
				{
					logger.Debug("Loading project plugin: {}", pluginProjectPath);

					pluginLastModifiedTime = fileStatus.lastModifiedTime;

					String strModified = ToString(fileStatus.lastModifiedTime);

					String tempBinPath = Path::Join(projectTempPath, "Binaries");

					if (!FileSystem::GetFileStatus(tempBinPath).exists)
					{
						FileSystem::CreateDirectory(tempBinPath);
					}

					String tempBinPathTime = Path::Join(tempBinPath, strModified);
					if (!FileSystem::GetFileStatus(tempBinPathTime).exists)
					{
						FileSystem::CreateDirectory(tempBinPathTime);
					}

					String newSharedLibFile = Path::Join(tempBinPathTime, Path::Name(pluginProjectPath) + SK_SHARED_EXT);
					FileSystem::CopyFile(pluginProjectPath, newSharedLibFile);

#ifdef SK_WIN
					String pdbFile = Path::Join(Path::Parent(pluginProjectPath), Path::Name(pluginProjectPath) + ".pdb");
					String newPdbFile = Path::Join(tempBinPathTime, Path::Name(pluginProjectPath) + ".pdb");
					FileSystem::CopyFile(pdbFile, newPdbFile);
#endif
					App::LoadPlugin(newSharedLibFile);
				}
			}
		}

		void EditorUpdate()
		{
			if (Input::GetCursorLockMode() == CursorLockMode::Locked)
			{
				ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
				ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
			}
			else
			{
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
			}

			LoadProjectPlugin();

			{
				std::scoped_lock lock(funcsMutex);
				while (!funcs.IsEmpty())
				{
					funcs.Dequeue()();
				}
			}

			ImGuiStyle& style = ImGui::GetStyle();
			ImGuiCreateDockSpace(dockSpaceId);
			DrawMenu();

			ProjectUpdate();
			DrawConfirmDialogs();
			InitDockSpace();
			DrawOpenWindows();

			if (showImGuiDemo)
			{
				ImGui::ShowDemoWindow(&showImGuiDemo);
			}

			ImGui::End();
		}

		void OnEditorShutdownRequest(bool* canClose)
		{
			if (forceClose) return;

			if (workspace->GetSceneEditor()->IsSimulationRunning())
			{
				workspace->GetSceneEditor()->StopSimulation();
			}

			GetUpdatedItems();

			if (!updatedItems.Empty())
			{
				*canClose = false;
				shouldOpenPopup = true;
			}
		}

		void EditorBeginFrame() {}
	}

	ConsoleSink& GetConsoleSink()
	{
		return consoleSink;
	}

	void Editor::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuContext.AddMenuItem(menuItem);
	}

	void Editor::OpenWindow(TypeID windowType, VoidPtr initUserData)
	{
		for (const EditorWindowStorage& window : editorWindowStorages)
		{
			if (window.typeId == windowType)
			{
				CreateWindow(window, initUserData);
				break;
			}
		}
	}

	void Editor::ShowConfirmDialog(StringView message, VoidPtr userData, FnConfirmCallback callback)
	{
		dialogModals.Enqueue(DialogModalData{message, userData, callback, DialogModalType::Confirmation});
	}

	void Editor::ShowErrorDialog(StringView message)
	{
		dialogModals.Enqueue(DialogModalData{message, nullptr, nullptr, DialogModalType::Error});
	}

	EditorWorkspace& Editor::GetCurrentWorkspace()
	{
		return *workspace;
	}

	UndoRedoScope* Editor::CreateUndoRedoScope(StringView name)
	{
		UndoRedoScope* scope = Resources::CreateScope(name);
		redoActions.Clear();
		undoActions.EmplaceBack(std::make_unique<UndoRedoScopeStorage>(scope));
		return scope;
	}

	void Editor::LockUndoRedo(bool lock)
	{
		undoRedoLocked = lock;
	}

	RID Editor::GetProject()
	{
		return projectRID;
	}

	Span<RID> Editor::GetPackages()
	{
		return packages;
	}

	RID Editor::LoadPackage(StringView name, StringView directory)
	{
		RID rid = ResourceAssets::ScanAssetsFromDirectory(name, directory);

		packages.EmplaceBack(rid);
		packagePaths.EmplaceBack(directory);

		return rid;
	}

	void Editor::ExecuteOnMainThread(std::function<void()> func)
	{
		std::scoped_lock lock(funcsMutex);
		funcs.Enqueue(func);
	}

	bool Editor::AnyWindowOfTypeHovered(TypeID windowType)
	{
		logger.Info("Hovered window on AnyWindowOfTypeHovered {}", ImGuiHoveredWindowId());

		for (const OpenWindowStorage& openWindow : openWindows)
		{
			if (openWindow.reflectType->GetProps().typeId == windowType)
			{
				if (openWindow.id == ImGuiHoveredWindowId())
				{
					return true;
				}
			}
		}
		return false;
	}

	bool Editor::DebugOptionsEnabled()
	{
		return debugOptionsEnabled;
	}

	void ImGuiDrawUndoRedoActions()
	{
		for (const auto& redo: redoActions)
		{
			StringView name = Resources::GetScopeName(redo->scope);
			ImGui::Selectable(!name.Empty() ? name.CStr() : "Unnamed action", false, ImGuiSelectableFlags_Disabled);
		}

		bool first = true;
		for (const auto& action: undoActions)
		{
			StringView name = Resources::GetScopeName(action->scope);
			ImGui::Selectable(!name.Empty() ? name.CStr() : "Unnamed action", first, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SpanAvailWidth);
			first = false;
		}
	}

	void ProjectBrowserWindowInit();
	void ResourceAssetsInit();

	void EditorInit(StringView projectFile)
	{
		if (projectFile.Empty())
		{
			logger.Error("Project path is empty");
			App::RequestShutdown();
			return;
		}

		projectPath = Path::Parent(projectFile);
		logger.Info("Initializing Editor with project: {}", projectFile);

		Event::Bind<OnUpdate, &EditorUpdate>();
		Event::Bind<OnShutdown, &Shutdown>();
		Event::Bind<OnShutdownRequest, &OnEditorShutdownRequest>();

		CreateMenuItems();
		ResourceAssetsInit();

		ShaderManagerInit();
		ProjectBrowserWindowInit();

		for (TypeID typeId : Reflection::GetDerivedTypes(TypeInfo<EditorWindow>::ID()))
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
			{
				EditorWindowProperties properties{};

				if (const EditorWindowProperties* editorWindowProperties = reflectType->GetAttribute<EditorWindowProperties>())
				{
					properties.createOnInit = editorWindowProperties->createOnInit;
					properties.dockPosition = editorWindowProperties->dockPosition;
					properties.order = editorWindowProperties->order;
				}

				editorWindowStorages.EmplaceBack(EditorWindowStorage{
					.typeId = reflectType->GetProps().typeId,
					.dockPosition = properties.dockPosition,
					.createOnInit = properties.createOnInit,
					.order = properties.order
				});
			}
		}

		std::sort(editorWindowStorages.begin(), editorWindowStorages.end(), [](const EditorWindowStorage& l, const EditorWindowStorage& r)
		{
			return l.order < r.order;
		});

		projectAssetPath = Path::Join(projectPath, "Assets");
		projectPackagePath = Path::Join(projectPath, "Packages");
		projectTempPath = Path::Join(projectPath, "Temp");
		projectSettingsPath =  Path::Join(projectPath, "ProjectSettings.cfg");
		pluginProjectPath = Path::Join(Path::Join(projectPath, "Binaries"), Path::Name(projectFile) + SK_SHARED_EXT);

		if (!FileSystem::GetFileStatus(projectSettingsPath).exists)
		{
			YamlArchiveWriter writer;
			Settings::CreateDefault(writer, TypeInfo<ProjectSettings>::ID());
			FileSystem::SaveFileAsString(projectSettingsPath, writer.EmitAsString());
		}

		{
			YamlArchiveReader reader(FileSystem::ReadFileAsString(projectSettingsPath));
			projectSettingsRID = Settings::Load(reader, TypeInfo<ProjectSettings>::ID());
			projectSettingsLastPersistedVersion = Resources::GetVersion(projectSettingsRID);
		}

		if (FileSystem::GetFileStatus(projectTempPath).exists)
		{
			FileSystem::Remove(projectTempPath);
		}

		FileSystem::CreateDirectory(projectTempPath);

		if (FileSystem::GetFileStatus(pluginProjectPath).exists)
		{
			LoadProjectPlugin();
		}

#ifdef SK_DEV_ASSETS
		Editor::LoadPackage("Skore", Path::Join(SK_ROOT_SOURCE_PATH, "Assets"));
#else
		//TODO - need to find a way to reload skore package only when it's updated.
		String skorePackagePath = Path::Join(projectPackagePath, "Skore");
		if (FileSystem::GetFileStatus(skorePackagePath).exists)
		{
			FileSystem::Remove(skorePackagePath);
		}
		StaticContent::SaveFilesToDirectory("Assets", skorePackagePath);
#endif

		for (auto& package: DirectoryEntries{projectPackagePath})
		{
#ifdef SK_DEV_ASSETS
			if (Path::Name(package) == "Skore")
			{
				continue;
			}
#endif
			Editor::LoadPackage(Path::Name(package), Path::Join(package, "Assets"));
		}

		projectRID = ResourceAssets::ScanAssetsFromDirectory(Path::Name(projectFile), projectAssetPath);

		workspace = std::make_unique<EditorWorkspace>();

		{
			RID sceneSettings = Settings::Get<ProjectSettings, SceneSettings>();
			if (ResourceObject sceneSettingsObject = Resources::Read(sceneSettings))
			{
				if (RID defaultEditorScene = sceneSettingsObject.GetReference(SceneSettings::DefaultEditorScene))
				{
					workspace->GetSceneEditor()->OpenEntity(defaultEditorScene);
				}
			}
		}

	}


	void RegisterResourceAssetTypes();
	void RegisterSceneEditorTypes();

	void EditorTypeRegister()
	{
		RegisterResourceAssetTypes();
		RegisterSceneEditorTypes();

		Reflection::Type<EditorWorkspace>();
		Reflection::Type<EditorWindow>();
		Reflection::Type<EditorWindowProperties>();
		Reflection::Type<ProjectBrowserWindow>();
		Reflection::Type<EntityTreeWindow>();
		Reflection::Type<SceneViewWindow>();
		Reflection::Type<TextureViewWindow>();
		Reflection::Type<HistoryWindow>();
		Reflection::Type<ConsoleWindow>();
		Reflection::Type<PropertiesWindow>();
		Reflection::Type<SettingsWindow>();
	}
}
