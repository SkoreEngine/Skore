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

#include "Asset/AssetEditor.hpp"
#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Window/ProjectBrowserWindow.hpp"

#include "EditorWorkspace.hpp"
#include "Asset/AssetFile.hpp"
#include "Commands/UndoRedoSystem.hpp"
#include "SDL3/SDL_process.h"
#include "Skore/Events.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Window/ConsoleWindow.hpp"
#include "Window/HistoryWindow.hpp"
#include "Window/PropertiesWindow.hpp"
#include "Window/SceneTreeWindow.hpp"
#include "Window/SceneViewWindow.hpp"
#include "Window/TextureViewWindow.hpp"

namespace Skore
{
	void AssetEditorInit();
	void AssetEditorShutdown();

	void ShaderManagerInit();
	void ShaderManagerShutdown();

	namespace
	{
		struct DialogModalData
		{
			String            message;
			VoidPtr           userData;
			FnConfirmCallback callback;
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

		Array<EditorWindowStorage> editorWindowStorages;
		Array<OpenWindowStorage>   openWindows;
		Array<AssetFile*>          updatedItems{};
		HashSet<AssetFile*>        ignoreSave{};
		Queue<DialogModalData>     confirmDialogs;
		ConsoleSink                consoleSink;

		bool forceClose = false;
		bool shouldOpenPopup = false;

		std::unique_ptr<EditorWorkspace> workspace;

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

		String projectPath;

		Logger& logger = Logger::GetLogger("Skore::Editor");

		void ShowImGuiDemo(const MenuItemEventData& eventData)
		{
			showImGuiDemo = true;
		}

		void Save()
		{
			for (AssetFile* asset : updatedItems)
			{
				if (!ignoreSave.Has(asset))
				{
					asset->Save();
				}
			}
			updatedItems.Clear();
			ignoreSave.Clear();
		}

		void SaveAll(const MenuItemEventData& eventData)
		{
			ignoreSave.Clear();
			AssetEditor::GetUpdatedAssets(updatedItems);
			Save();
		}

		void CloseEngine(const MenuItemEventData& eventData)
		{
			App::RequestShutdown();
		}

		void Undo(const MenuItemEventData& eventData)
		{
			UndoRedoSystem::Undo();
		}

		bool UndoEnabled(const MenuItemEventData& eventData)
		{
			return UndoRedoSystem::CanUndo();
		}

		void Redo(const MenuItemEventData& eventData)
		{
			UndoRedoSystem::Redo();
		}

		bool RedoEnabled(const MenuItemEventData& eventData)
		{
			return UndoRedoSystem::CanRedo();
		}

		bool CreateCMakeProjectEnabled(const MenuItemEventData& eventData)
		{



			// return AssetEditor::CanCreateCMakeProject();
			return false;
		}

		void CreateCMakeProject(const MenuItemEventData& eventData)
		{
			//system("clion C:\\dev\\SkoreEngine\\Projects\\TestCpp");
			// const char *args[] = { "clion.exe", "", NULL };
			// SDL_Process* process = SDL_CreateProcess(args, false);
			// if (!process)
			// {
			// 	logger.Error("Failed to create process: {}", SDL_GetError());
			// }
			//
			// int a = 0;

			//AssetEditor::CreateCMakeProject();
		}

		void Build(const MenuItemEventData& eventData)
		{
			// String path;
			// if (Platform::PickFolder(path, {}) == DialogResult::OK)
			// {
			//     auto stat = FileSystem::GetFileStatus(path);
			//     if (!stat.exists)
			//     {
			//         FileSystem::CreateDirectory(path);
			//     }
			//     AssetEditor::Export(path);
			// }
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

		void CreateMenuItems()
		{
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File", .priority = 0});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/New Scene", .priority = 0, .action = NewProject});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Open Scene", .priority = 10, .action = OpenScene});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Recent Scenes", .priority = 10, .action = OpenScene});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Save All", .priority = 1000, .itemShortcut{.ctrl = true, .presKey = Key::S}, .action = SaveAll});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Build Settings...", .priority = 2000, .action = Build});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Build", .priority = 2000, .action = Build});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "File/Exit", .priority = I32_MAX, .itemShortcut{.ctrl = true, .presKey = Key::Q}, .action = CloseEngine});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit", .priority = 30});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Undo", .priority = 10, .itemShortcut{.ctrl = true, .presKey = Key::Z}, .action = Undo, .enable = UndoEnabled});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Redo", .priority = 20, .itemShortcut{.ctrl = true, .shift = true, .presKey = Key::Z}, .action = Redo, .enable = RedoEnabled});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Editor Preferences...", .priority = 1000, .action = SettingsWindow::Open, .userData = GetTypeID<EditorPreferences>()});
			// Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Project Settings...", .priority = 1010, .action = SettingsWindow::Open, .userData = GetTypeID<ProjectSettings>()});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools", .priority = 50});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Create CMake Project", .priority = 10, .action = CreateCMakeProject, .enable = CreateCMakeProjectEnabled});
			Editor::AddMenuItem(MenuItemCreation{.itemName = "Tools/Reload Shaders", .priority = 100, .itemShortcut = {.presKey = Key::F5}, .action = ReloadShaders});
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

			openWindows.Clear();
			openWindows.ShrinkToFit();
			editorWindowStorages.Clear();
			editorWindowStorages.ShrinkToFit();
			idCounter = 100000;


			// projectFile.Clear();

			ShaderManagerShutdown();
			AssetEditorShutdown();
			UndoRedoSystem::Shutdown();
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
				bottomRightDockId = ImGui::DockBuilderSplitNode(bottomLeftDockId, ImGuiDir_Right, 0.30f, nullptr, &bottomLeftDockId);

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

								for (AssetFile* asset : updatedItems)
								{
									ImGui::TableNextRow();

									ImGui::TableNextColumn();

									ImGui::BeginHorizontal(asset);
									ImGui::Spring(1.0);

									bool shouldUpdate = !ignoreSave.Has(asset);

									if (ImGui::Checkbox("###", &shouldUpdate))
									{
										if (!shouldUpdate)
										{
											ignoreSave.Insert(asset);
										}
										else
										{
											ignoreSave.Erase(asset);
										}
									}

									ImGui::Spring(1.0);
									ImGui::EndHorizontal();

									ImGui::TableNextColumn();
									ImGui::Text("%s", asset->GetFileName().CStr());
									ImGui::TableNextColumn();
									ImGui::Text("%s", asset->GetPath().CStr());
									ImGui::TableNextColumn();


									if (asset->GetPersistedVersion() == 0)
									{
										ImGui::TextColored(ImVec4(0.1, 0.8, 0.1, 1), "Created");
									}
									else
									{
										ImGui::Text("Updated");
									}
								}
								ImGui::EndTable();
							}

							ImGui::EndChild();
						}

						ImGui::BeginHorizontal("#horizontal-save", ImVec2(width, buttonHeight));

						if (ImGui::Button("Select All"))
						{
							ignoreSave.Clear();
						}

						if (ImGui::Button("Unselect All"))
						{
							for (AssetFile* assetFile : updatedItems)
							{
								ignoreSave.Insert(assetFile);
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
			if (!confirmDialogs.IsEmpty())
			{
				const DialogModalData& modal = confirmDialogs.Peek();
				ImGui::OpenPopup("Confirmation");

				ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				if (ImGui::BeginPopupModal("Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				{
					ImGui::Text(modal.message.CStr());
					ImGui::Separator();

					if (ImGui::Button("OK", ImVec2(120, 0)))
					{
						ImGui::CloseCurrentPopup();
						if (modal.callback)
						{
							modal.callback(modal.userData);

						}
						confirmDialogs.Dequeue();
					}

					ImGui::SetItemDefaultFocus();

					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(120, 0)))
					{
						ImGui::CloseCurrentPopup();
						confirmDialogs.Dequeue();
					}
					ImGui::EndPopup();
				}
			}
		}

		void EditorUpdate()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			ImGuiCreateDockSpace(dockSpaceId);
			ProjectUpdate();
			DrawConfirmDialogs();
			InitDockSpace();
			DrawOpenWindows();

			if (showImGuiDemo)
			{
				ImGui::ShowDemoWindow(&showImGuiDemo);
			}

			DrawMenu();
			ImGui::End();
		}

		void OnEditorShutdownRequest(bool* canClose)
		{
			if (forceClose) return;

			// if (sceneEditor->IsSimulating())
			// {
			// 	sceneEditor->StopSimulation();
			// 	*canClose = false;
			// 	return;
			// }

			updatedItems.Clear();
			AssetEditor::GetUpdatedAssets(updatedItems);

			if (!updatedItems.Empty())
			{
				*canClose = false;
				shouldOpenPopup = true;
			}
		}

		void EditorBeginFrame()
		{

		}
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
		confirmDialogs.Enqueue(DialogModalData{message, userData, callback});
	}

	EditorWorkspace& Editor::GetCurrentWorkspace()
	{
		return *workspace;
	}

	void RegisterAssetTypes();

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

		AssetEditorInit();
		ShaderManagerInit();
		UndoRedoSystem::Initialize();

		workspace = std::make_unique<EditorWorkspace>();

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

		AssetEditor::AddPackage("Skore", FileSystem::AssetFolder());
		AssetEditor::SetProject(Path::Name(projectPath), projectPath);
	}

	void EditorTypeRegister()
	{
		RegisterAssetTypes();

		Reflection::Type<EditorWindow>();
		Reflection::Type<EditorWindowProperties>();
		Reflection::Type<ProjectBrowserWindow>();
		Reflection::Type<SceneTreeWindow>();
		Reflection::Type<SceneViewWindow>();
		Reflection::Type<TextureViewWindow>();
		Reflection::Type<HistoryWindow>();
		Reflection::Type<ConsoleWindow>();
		Reflection::Type<PropertiesWindow>();
	}
}
