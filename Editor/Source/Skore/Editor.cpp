#include "Skore/Editor.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

#include "Skore/EditorLayout.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Utils/ProjectUtils.hpp"
#include "Skore/Window/ConsoleWindow.hpp"
#include "Skore/Window/EntityTreeWindow.hpp"
#include "Skore/Window/HistoryWindow.hpp"
#include "Skore/Window/PropertiesWindow.hpp"
#include "Skore/Window/ResourceDebuggerWindow.hpp"
#include "Skore/Window/SceneViewWindow.hpp"
#include "Skore/Window/TextureViewWindow.hpp"
#include "Skore/Window/GraphEditorWindow.hpp"

#include <thread>
#include <chrono>

#include "Skore/ImGui/Icons.h"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/ThreadPool.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/IO/Input.hpp"
#include "Skore/Utils/StaticContent.hpp"
#include "Skore/Window/SettingsWindow.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Window/AnimatorGraphWindow.hpp"
#include "Skore/Window/AnimatorTreeViewWindow.hpp"
#include "Skore/Window/DebuggerWindow.hpp"

namespace Skore
{
	void ShaderManagerInit();
	void ShaderManagerShutdown();

	struct EditorState
	{
		enum
		{
			ActiveWorkspaceIndex
		};
	};

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

		Array<EditorWindowStorage>      editorWindowStorages;
		Array<EditorWorkspaceTypeDesc>  editorWorkspaceTypeDescs;
		Queue<DialogModalData>          dialogModals;
		ConsoleSink                consoleSink;

		struct ProjectUpdatedItems
		{
			RID rid;
			String path;
			Array<UpdatedAssetInfo> updatedItems;
		};

		RID                     projectRID;
		String                  projectPath;
		String                  projectAssetPath;
		String								  projectLocalPath;
		String                  projectTempPath;
		String                  projectPackagePath;


		String projectSettingsPath;
		String projectTypesPath;
		RID    projectSettingsRID;
		u64    projectSettingsLastPersistedVersion = 0;

		//c++ dev plugin
		String                   pluginProjectPath;
		u64                      pluginLastModifiedTime = 0;

		Array<RID>    packages;
		Array<String> packagePaths;

		Array<ProjectUpdatedItems> updatedItems;

		bool forceClose = false;
		bool shouldOpenPopup = false;

		Array<std::unique_ptr<EditorWorkspace>> workspaces;
		u32 activeWorkspaceIndex = 0;
		Array<u32> workspacesToRemove;
		RID editorState;

		void OnEditorStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			u32 newIndex = static_cast<u32>(newValue.GetUInt(EditorState::ActiveWorkspaceIndex));
			if (newIndex < workspaces.Size())
			{
				activeWorkspaceIndex = newIndex;
			}
		}

		void SwitchWorkspace(u32 index)
		{
			if (index == activeWorkspaceIndex || index >= workspaces.Size()) return;
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Switch Workspace");
			ResourceObject stateObj = Resources::Write(editorState);
			stateObj.SetUInt(EditorState::ActiveWorkspaceIndex, index);
			stateObj.Commit(scope);
		}

		Array<std::unique_ptr<UndoRedoScopeStorage>> undoActions{};
		Array<std::unique_ptr<UndoRedoScopeStorage>> redoActions{};
		bool undoRedoLocked = false;

		bool debugOptionsEnabled = false;

		std::mutex funcsMutex;
		Queue<std::function<void()>> funcs;

		std::unique_ptr<ThreadPool> threadPool;

		MenuItemContext menuContext{};
		bool            showImGuiDemo = false;

		Logger& logger = Logger::GetLogger("Skore::Editor");

		void ShowImGuiDemo(const MenuItemEventData& eventData)
		{
			showImGuiDemo = true;
		}

		void ResetLayoutAction(const MenuItemEventData& eventData)
		{
			EditorLayout::ResetAll();
			for (auto& ws : workspaces)
			{
				ws->ResetLayout();
			}
		}

		void GetUpdatedItems()
		{
			updatedItems.Clear();

			for (int i = 0; i < packages.Size(); ++i)
			{
				if (Array<UpdatedAssetInfo> items = ResourceAssets::GetUpdatedAssets(packages[i]); !items.Empty())
				{
					updatedItems.EmplaceBack(ProjectUpdatedItems{
						.rid = packages[i],
						.path = Path::Join(packagePaths[i], "Assets"),
						.updatedItems = Traits::Move(items)
					});
				}
			}

			if (Array<UpdatedAssetInfo> items = ResourceAssets::GetUpdatedAssets(projectRID); !items.Empty())
			{
				updatedItems.EmplaceBack(ProjectUpdatedItems{
					.rid = projectRID,
					.path = projectAssetPath,
					.updatedItems = Traits::Move(items)
				});
			}
		}

		void Save()
		{
			for (const ProjectUpdatedItems& updatedItem : updatedItems)
			{
				if (updatedItem.updatedItems.Empty())
				{
					continue;
				}
				logger.Debug("Saving {} assets to {}", updatedItem.updatedItems.Size(), updatedItem.path);
				ResourceAssets::SaveAssetsToDirectory(updatedItem.path, updatedItem.rid, updatedItem.updatedItems);
			}

			JsonArchiveWriter typeWriter;
			Resources::SerializeTypes(typeWriter);

			FileSystem::SaveFileAsString(projectTypesPath, typeWriter.EmitAsString());
			logger.Debug("Saved resource types to {}", projectTypesPath);

			updatedItems.Clear();
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
			if (EditorWorkspace* ws = Editor::GetActiveWorkspace())
				return ws->GetSceneEditor()->HasSelectedEntities();
			return false;
		}

		void Duplicate(const MenuItemEventData& eventData)
		{
			if (EditorWorkspace* ws = Editor::GetActiveWorkspace())
				ws->GetSceneEditor()->DuplicateSelected();
		}

		void Delete(const MenuItemEventData& eventData)
		{
			if (EditorWorkspace* ws = Editor::GetActiveWorkspace())
				ws->GetSceneEditor()->DestroySelected();
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
			String projectName = Path::Name(projectPath);

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

			String assetsPath = Path::Join(exportPath, "Assets");
			FileSystem::CreateDirectory(assetsPath);

			//export packages
			{
				Array<RID> packagesToExport;
				packagesToExport.Reserve(packages.Size() + 1);
				for (RID package : packages)
				{
					packagesToExport.EmplaceBack(package);
				}
				packagesToExport.EmplaceBack(projectRID);
				ResourceAssets::ExportPackages(packagesToExport, assetsPath, projectName);
			}

			logger.Debug("Project exported to {}", Path::Join(projectPath, "Export"));

			if (run)
			{
				String command = Path::Join(exportPath, Path::Name(projectPath) + SK_EXEC_EXT);
				const char *args[] = { command.CStr(), "--current-path", exportPath.CStr(), NULL };
				if (VoidPtr process = Platform::CreateProcess(args, true))
				{
					Platform::DestroyProcess(process);
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
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Build", .priority = 55});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools", .priority = 50});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Open Editor", .priority = 5, .action = OpenProjectInEditorAction, .visible = CanOpenEditor});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Create CMake Project", .priority = 10, .action = CreateCMakeProject, .visible = CreateCMakeProjectVisible});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Reload Shaders", .priority = 100, .itemShortcut = {.presKey = Key::F5}, .action = ReloadShaders});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Show Debug Options", .priority = 105, .action = ShowDebugOptions, .selected = IsDebugOptionsEnabled});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Window", .priority = 60});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Help", .priority = 70});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Reset Layout", .priority = I32_MAX - 1, .action = ResetLayoutAction});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Dear ImGui Demo", .priority = I32_MAX, .action = ShowImGuiDemo});
		}

		void Shutdown()
		{
			EditorLayout::Shutdown();

			threadPool = {};
			menuContext = {};

			Resources::FindType<EditorState>()->UnregisterEvent(ResourceEventType::Changed, OnEditorStateChange, nullptr);
			Resources::Destroy(editorState);
			editorState = {};

			for (auto& ws : workspaces)
			{
				ws->DestroyAllWindows();
			}
			workspaces.Clear();
			activeWorkspaceIndex = 0;

			editorWindowStorages.Clear();
			editorWindowStorages.ShrinkToFit();

			editorWorkspaceTypeDescs.Clear();
			editorWorkspaceTypeDescs.ShrinkToFit();

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

				if (ImGui::BeginTabBar("workspaces-tab"))
				{
					for (u32 i = 0; i < workspaces.Size(); ++i)
					{
						ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
						if (i == activeWorkspaceIndex)
						{
							flags |= ImGuiTabItemFlags_SetSelected;
						}

						bool open = true;

						char str[128];
						snprintf(str, sizeof(str), "%s###ws_%d", workspaces[i]->GetName().CStr(), workspaces[i]->GetId());

						bool showClose = workspaces.Size() > 1;
						if (ImGui::BeginTabItem(str, showClose ? &open : nullptr, flags))
						{
							ImGui::EndTabItem();
						}

						if (ImGui::IsItemClicked())
						{
							SwitchWorkspace(i);
						}

						if (!open && workspaces.Size() > 1)
						{
							workspacesToRemove.EmplaceBack(i);
						}
					}

					if (ImGui::TabItemButton("+"))
					{
						ImGui::OpenPopup("workspace-type-popup");
					}

					if (ImGui::BeginPopup("workspace-type-popup"))
					{
						for (const EditorWorkspaceTypeDesc& wsType : editorWorkspaceTypeDescs)
						{
							if (ImGui::MenuItem(String(wsType.displayName).CStr()))
							{
								workspaces.EmplaceBack(std::make_unique<EditorWorkspace>(wsType.id));
								SwitchWorkspace(workspaces.Size() - 1);
							}
						}
						ImGui::EndPopup();
					}
					ImGui::EndTabBar();
				}

				ImGui::EndMenuBar();
			}
			ImGui::PopStyleColor(1);
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

								for (ProjectUpdatedItems& project : updatedItems)
								{
									for (UpdatedAssetInfo& info : project.updatedItems)
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
								}
								ImGui::EndTable();
							}

							ImGui::EndChild();
						}

						ImGui::BeginHorizontal("#horizontal-save", ImVec2(width, buttonHeight));

						if (ImGui::Button("Select All"))
						{
							for (ProjectUpdatedItems& project : updatedItems)
							{
								for (UpdatedAssetInfo& info : project.updatedItems)
								{
									info.shouldUpdate = true;
								}
							}
						}

						if (ImGui::Button("Unselect All"))
						{
							for (ProjectUpdatedItems& project : updatedItems)
							{
								for (UpdatedAssetInfo& info : project.updatedItems)
								{
									info.shouldUpdate = false;
								}
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

					String tempBinPath = Path::Join(projectTempPath, "HotReload");

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

			if (workspaces.Size() > 1 && Input::IsKeyDown(Key::LeftShift) && Input::IsKeyPressed(Key::Tab))
			{
				SwitchWorkspace((activeWorkspaceIndex + 1) % workspaces.Size());
			}

			ImGuiStyle& style = ImGui::GetStyle();
			auto& active = *workspaces[activeWorkspaceIndex];
			ImGuiCreateDockSpace(active.GetDockSpaceId());
			DrawMenu();

			ProjectUpdate();
			DrawConfirmDialogs();
			active.InitDockSpace();
			active.DrawWindows();

			EditorLayout::Flush();

			if (showImGuiDemo)
			{
				ImGui::ShowDemoWindow(&showImGuiDemo);
			}

			ImGui::BeginHorizontal("horizontal-save", ImVec2(ImGui::GetContentRegionAvail().x - 5, 0));
			ImGui::Text("%.2f ms (%.2f FPS)", App::DeltaTime() * 1000, App::GetFPS());
			ImGui::Spring(1);

			u64 size = threadPool->Size();
			if (size > 0)
			{
				if (size == 1)
				{
					ImGui::Text("1 Pending Task");
				}
				else
				{
					ImGui::Text("%u Pending Tasks", size);
				}
			}
			else
			{
				ImGui::Text("Idle");
			}
			//ImGui::ProgressBar(sinf((float)ImGui::GetTime()) * 0.5f + 0.5f, ImVec2(250 * style.ScaleFactor, 18 * style.ScaleFactor), " 0 / 0 tasks done");
			ImGui::EndHorizontal();

			ImGui::End();

			// Deferred workspace removal — must happen after all drawing/events for this frame
			if (!workspacesToRemove.Empty())
			{
				// Remove in reverse order so indices stay valid
				for (i32 r = workspacesToRemove.Size() - 1; r >= 0; --r)
				{
					u32 idx = workspacesToRemove[r];
					workspaces[idx]->DestroyAllWindows();
					workspaces.Erase(workspaces.begin() + idx, workspaces.begin() + idx + 1);
				}
				workspacesToRemove.Clear();

				if (activeWorkspaceIndex >= workspaces.Size())
				{
					u32 newIndex = workspaces.Size() > 0 ? workspaces.Size() - 1 : 0;
					ResourceObject stateObj = Resources::Write(editorState);
					stateObj.SetUInt(EditorState::ActiveWorkspaceIndex, newIndex);
					stateObj.Commit();
				}
			}

			for (auto& ws : workspaces)
			{
				ws->DoUpdate();
			}
		}

		void OnEditorShutdownRequest(bool* canClose)
		{
			// if (threadPool && threadPool->Size() > 0)
			// {
			// 	*canClose = false;
			// 	return;
			// }

			if (forceClose) return;

			for (auto& ws : workspaces)
			{
				if (ws->GetSceneEditor()->IsSimulationRunning())
				{
					ws->GetSceneEditor()->StopSimulation();
				}
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

	SK_API ConsoleSink& GetConsoleSink()
	{
		return consoleSink;
	}

	void Editor::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuContext.AddMenuItem(menuItem);
	}

	void Editor::ShowConfirmDialog(StringView message, VoidPtr userData, FnConfirmCallback callback)
	{
		dialogModals.Enqueue(DialogModalData{message, userData, callback, DialogModalType::Confirmation});
	}

	void Editor::ShowErrorDialog(StringView message)
	{
		dialogModals.Enqueue(DialogModalData{message, nullptr, nullptr, DialogModalType::Error});
	}

	EditorWorkspace* Editor::GetActiveWorkspace()
	{
		if (workspaces.Empty()) return nullptr;
		return workspaces[activeWorkspaceIndex].get();
	}

	EditorWorkspace* Editor::GetWorkspaceOfType(u8 type)
	{
		EditorWorkspace* selectedWorkspace = nullptr;

		for (auto& workspace : workspaces)
		{
			if (workspace && workspace->GetWorkspaceTypeId() == type)
			{
				selectedWorkspace = workspace.get();
			}
		}

		if (selectedWorkspace == nullptr)
		{
			selectedWorkspace = CreateWorkspace(type);
		}

		SwitchWorkspace(selectedWorkspace->GetId());

		return selectedWorkspace;
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

	StringView Editor::GetProjectPath()
	{
		return projectPath;
	}

	StringView Editor::GetProjectTempPath()
	{
		return projectTempPath;
	}

	StringView Editor::GetProjectLocalPath()
	{
		return projectLocalPath;
	}

	Span<RID> Editor::GetPackages()
	{
		return packages;
	}

	RID Editor::LoadPackage(StringView name, StringView directory)
	{
		RID rid = ResourceAssets::ScanPackageFromDirectory(name, directory);

		packages.EmplaceBack(rid);
		packagePaths.EmplaceBack(directory);

		return rid;
	}

	void Editor::ExecuteOnMainThread(std::function<void()> func)
	{
		std::scoped_lock lock(funcsMutex);
		funcs.Enqueue(func);
	}

	void Editor::AddTask(std::function<void()> func, StringView name)
	{
		threadPool->Enqueue(Traits::Move(func));
	}

	EditorWorkspace* Editor::CreateWorkspace(u8 type)
	{
		workspaces.EmplaceBack(std::make_unique<EditorWorkspace>(type));
		SwitchWorkspace(workspaces.Size() - 1);
		return GetActiveWorkspace();
	}

	bool Editor::DebugOptionsEnabled()
	{
		return debugOptionsEnabled;
	}

	u32 Editor::GetWorkspaceCount()
	{
		return workspaces.Size();
	}

	void Editor::SetActiveWorkspace(u32 index)
	{
		SwitchWorkspace(index);
	}

	Span<EditorWindowStorage> GetEditorWindowStorages()
	{
		return editorWindowStorages;
	}

	Span<EditorWorkspaceTypeDesc> GetEditorWorkspaceTypeStorages()
	{
		return editorWorkspaceTypeDescs;
	}

	u32 AllocateWindowId()
	{
		for (u32 candidate = 1; candidate != 0; ++candidate)
		{
			bool inUse = false;
			for (const auto& ws : workspaces)
			{
				if (ws->IsWindowIdInUse(candidate))
				{
					inUse = true;
					break;
				}
			}
			if (!inUse) return candidate;
		}
		return 0;
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

	SK_API void EditorInit(StringView projectFile)
	{

		if (projectFile.Empty())
		{
			logger.Error("Project file is empty");
			App::RequestShutdown();
			return;
		}

		if (Path::Extension(projectFile) != ".skore")
		{
			auto error = fmt::format("Project file \"{}\" is not a skore project, \nEngine will close", projectFile);
			Platform::ShowSimpleMessageBox(MessageBoxType::Error, "Initialization Error", error.c_str(), {});
			App::RequestShutdown();
			return;
		}

		threadPool = std::make_unique<ThreadPool>();
		projectPath = Path::Parent(projectFile);

		projectAssetPath = Path::Join(projectPath, "Assets");
		projectPackagePath = Path::Join(projectPath, "Packages");
		projectLocalPath = Path::Join(projectPath, "Local");
		projectTempPath = Path::Join(projectLocalPath, "Temp");
		projectSettingsPath =  Path::Join(projectPath, "ProjectSettings.cfg");
		projectTypesPath = Path::Join(projectPath, "types.json");

		if (FileSystem::GetFileStatus(projectTypesPath).exists)
		{
			//JsonArchiveReader typesReader(FileSystem::ReadFileAsString(projectTypesPath));
			//u32 importedTypes = Resources::DeserializeTypes(typesReader);
			//logger.Debug("Loaded {} resource types from {}", importedTypes, projectTypesPath);
		}

		//TODO - not sure?
		pluginProjectPath = Path::Join(Path::Join(projectPath, "Binaries"), Path::Name(projectFile) + SK_SHARED_EXT);


		if (!FileSystem::GetFileStatus(projectAssetPath).exists)
		{
			FileSystem::CreateDirectory(projectAssetPath);
		}

		if (!FileSystem::GetFileStatus(projectPackagePath).exists)
		{
			FileSystem::CreateDirectory(projectPackagePath);
		}

		if (!FileSystem::GetFileStatus(projectLocalPath).exists)
		{
			FileSystem::CreateDirectory(projectLocalPath);
		}


		if (FileSystem::GetFileStatus(projectTempPath).exists)
		{
			FileSystem::Remove(projectTempPath);
		}

		FileSystem::CreateDirectory(projectTempPath);


		logger.Info("Initializing Editor with project: {}", projectFile);

		Event::Bind<OnUpdate, &EditorUpdate>();
		Event::Bind<OnShutdown, &Shutdown>();
		Event::Bind<OnShutdownRequest, &OnEditorShutdownRequest>();

		CreateMenuItems();
		ResourceAssetsInit();

		ShaderManagerInit();
		ProjectBrowserWindowInit();
		EditorLayout::Init();

		for (TypeID typeId : Reflection::GetDerivedTypes(TypeInfo<EditorWindow>::ID()))
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
			{
				EditorWindowProperties properties{};

				if (const EditorWindowProperties* editorWindowProperties = reflectType->GetAttribute<EditorWindowProperties>())
				{
					properties.dockPosition = editorWindowProperties->dockPosition;
					properties.order = editorWindowProperties->order;
					properties.workspaceTypes = editorWindowProperties->workspaceTypes;
				}

				editorWindowStorages.EmplaceBack(EditorWindowStorage{
					.typeId = reflectType->GetProps().typeId,
					.dockPosition = properties.dockPosition,
					.order = properties.order,
					.workspaceTypes = properties.workspaceTypes
				});
			}
		}

		std::sort(editorWindowStorages.begin(), editorWindowStorages.end(), [](const EditorWindowStorage& l, const EditorWindowStorage& r)
		{
			return l.order < r.order;
		});

		logger.Debug("projectSettingsPath {}", projectSettingsPath);
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

		if (FileSystem::GetFileStatus(pluginProjectPath).exists)
		{
			LoadProjectPlugin();
		}

#ifdef SK_DEV_ASSETS
		Editor::LoadPackage("Skore", Path::Join(SK_ROOT_SOURCE_PATH));
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
			Editor::LoadPackage(Path::Name(package), package);
		}

		projectRID = ResourceAssets::ScanPackageFromDirectory(Path::Name(projectFile), projectPath);

		editorWorkspaceTypeDescs.EmplaceBack(EditorWorkspaceTypeDesc{
			.id = WorkspaceTypes::Scene,
			.displayName = "Scene",
			.order = 0
		});

		editorWorkspaceTypeDescs.EmplaceBack(EditorWorkspaceTypeDesc{
			.id = WorkspaceTypes::Graph,
			.displayName = "Graph",
			.order = 1
		});

		editorWorkspaceTypeDescs.EmplaceBack(EditorWorkspaceTypeDesc{
			.id = WorkspaceTypes::Animator,
			.displayName = "Animator",
			.order = 2
		});

		workspaces.EmplaceBack(std::make_unique<EditorWorkspace>(WorkspaceTypes::Scene));

		editorState = Resources::Create<EditorState>();
		{
			ResourceObject stateObj = Resources::Write(editorState);
			stateObj.SetUInt(EditorState::ActiveWorkspaceIndex, 0);
			stateObj.Commit();
		}
		Resources::FindType<EditorState>()->RegisterEvent(ResourceEventType::Changed, OnEditorStateChange, nullptr);

		{
			RID sceneSettings = Settings::Get<ProjectSettings, SceneSettings>();
			if (ResourceObject sceneSettingsObject = Resources::Read(sceneSettings))
			{
				if (RID defaultEditorScene = sceneSettingsObject.GetReference(SceneSettings::DefaultEditorScene))
				{
					workspaces[0]->GetSceneEditor()->OpenEntity(defaultEditorScene);
				}
			}
		}

	}


	void RegisterResourceAssetTypes();
	void RegisterSceneEditorTypes();
	void RegisterSceneViewPipelineModule();
	void RegisterThumbnailGenerationTypes();
	void RegisterAnimatorEditorTypes();

	SK_API void EditorTypeRegister()
	{
		RegisterResourceAssetTypes();
		RegisterSceneEditorTypes();
		RegisterSceneViewPipelineModule();

		Resources::Type<EditorState>()
			.Field<EditorState::ActiveWorkspaceIndex>(ResourceFieldType::UInt)
			.Build();

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
		Reflection::Type<ResourceDebuggerWindow>();
		Reflection::Type<SettingsWindow>();
		Reflection::Type<GraphEditorWindow>();
		Reflection::Type<DebuggerWindow>();
		Reflection::Type<AnimatorTreeViewWindow>();
		Reflection::Type<AnimatorGraphWindow>();

		RegisterThumbnailGenerationTypes();
		RegisterAnimatorEditorTypes();
	}
}
