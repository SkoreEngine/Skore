#include "Editor.hpp"

#include <mutex>

#include "EditorTypes.hpp"
#include "Action/EditorAction.hpp"
#include "Asset/AssetTypes.hpp"
#include "Skore/Engine.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Lib/imgui_internal.h"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"
#include "Scene/SceneEditor.hpp"
#include "Window/ProjectBrowserWindow.hpp"
#include "Window/PropertiesWindow.hpp"
#include "Window/SceneTreeWindow.hpp"
#include "Window/SceneViewWindow.hpp"
#include "Window/SettingsWindow.hpp"
#include "Window/TextureViewWindow.hpp"

namespace Skore
{
    void InitEditorAction();
    void RegisterAssetTypes();
    void AssetEditorInit();
    void ImGuiUpdate();
    void ImGuiShutdown();
    void RegisterFieldRenderers();
    void ShaderManagerInit();
    void ShaderManagerShutdown();
    void RegistrySceneEditorTypes();

    struct EditorWindowStorage
    {
        TypeID       typeId{};
        FnCast       fnCast{};
        DockPosition dockPosition{};
        bool         createOnInit{};
    };

    struct OpenWindowStorage
    {
        u32           id{};
        EditorWindow* instance{};
        TypeHandler*  typeHandler{};
    };

    namespace
    {
        Array<EditorWindowStorage> editorWindowStorages{};
        Array<OpenWindowStorage>   openWindows{};
        Array<AssetFile*>          updatedItems{};
        bool                       shouldOpenPopup = false;

        std::mutex                   callsMutex;
        Array<std::function<void()>> calls{};

        MenuItemContext menuContext{};
        bool            dockInitialized = false;
        u32             dockSpaceId{10000};
        u32             centerSpaceId{10000};
        u32             topRightDockId{};
        u32             bottomRightDockId{};
        u32             bottomDockId{};
        u32             leftDockId{};
        u32             idCounter{100000};
        bool            showImGuiDemo = false;

        bool forceClose{};

        Array<SharedPtr<EditorTransaction>> undoActions{};
        Array<SharedPtr<EditorTransaction>> redoActions{};

        SharedPtr<SceneEditor> sceneEditor{};
        String                 projectFile;

        void SaveAll(Span<AssetFile*> assets);

        void Shutdown()
        {
            sceneEditor = {};
            menuContext = {};

            for (OpenWindowStorage& openWindow : openWindows)
            {
                if (openWindow.instance)
                {
                    openWindow.typeHandler->Destroy(openWindow.instance);
                }
            }

            ImGuiShutdown();
            ShaderManagerShutdown();

            openWindows.Clear();
            openWindows.ShrinkToFit();
            editorWindowStorages.Clear();
            editorWindowStorages.ShrinkToFit();
            idCounter = 100000;

            undoActions.Clear();
            undoActions.ShrinkToFit();

            redoActions.Clear();
            redoActions.ShrinkToFit();

            calls.Clear();
            calls.ShrinkToFit();

            projectFile.Clear();
        }

        void InitEditor()
        {
            //TODO - this needs to be update on reload.
            TypeHandler*      editorWindow = Registry::FindType<EditorWindow>();
            Span<DerivedType> derivedTypes = editorWindow->GetDerivedTypes();
            for (const DerivedType& derivedType : derivedTypes)
            {
                EditorWindowProperties properties{};
                TypeHandler*           typeHandler = Registry::FindTypeById(derivedType.typeId);
                if (typeHandler)
                {
                    const EditorWindowProperties* editorWindowProperties = typeHandler->GetAttribute<EditorWindowProperties>();
                    if (editorWindowProperties)
                    {
                        properties.createOnInit = editorWindowProperties->createOnInit;
                        properties.dockPosition = editorWindowProperties->dockPosition;
                    }
                }

                editorWindowStorages.EmplaceBack(EditorWindowStorage{
                    .typeId = derivedType.typeId,
                    .fnCast = derivedType.fnCast,
                    .dockPosition = properties.dockPosition,
                    .createOnInit = properties.createOnInit
                });
            }

            sceneEditor = MakeShared<SceneEditor>();

            AssetEditorInit();

            JsonArchiveReader reader(FileSystem::ReadFileAsString(projectFile));
            AssetEditor::AddPackage("Skore", FileSystem::AssetFolder());
            AssetEditor::SetProject(Path::Name(projectFile), Path::Parent(projectFile));
        }

        void CloseEngine(const MenuItemEventData& eventData)
        {
            Engine::Shutdown();
        }

        void NewProject(const MenuItemEventData& eventData) {}

        void SaveAll(const MenuItemEventData& eventData)
        {
            AssetEditor::GetUpdatedAssets(updatedItems);
            SaveAll(updatedItems);
            updatedItems.Clear();
        }

        void ShowImGuiDemo(const MenuItemEventData& eventData)
        {
            showImGuiDemo = true;
        }

        void Undo(const MenuItemEventData& eventData)
        {
            SharedPtr<EditorTransaction> action = undoActions.Back();
            action->Rollback();
            redoActions.EmplaceBack(action);
            undoActions.PopBack();
        }

        bool UndoEnabled(const MenuItemEventData& eventData)
        {
            return !undoActions.Empty();
        }

        void Redo(const MenuItemEventData& eventData)
        {
            SharedPtr<EditorTransaction> action = redoActions.Back();
            action->Commit();

            redoActions.PopBack();
            undoActions.EmplaceBack(action);
        }

        bool RedoEnabled(const MenuItemEventData& eventData)
        {
            return !redoActions.Empty();
        }

        bool CreateCMakeProjectEnabled(const MenuItemEventData& eventData)
        {
            return AssetEditor::CanCreateCMakeProject();
        }

        void CreateCMakeProject(const MenuItemEventData& eventData)
        {
            AssetEditor::CreateCMakeProject();
        }

        void OpenScene(const MenuItemEventData& eventData)
        {

        }

        void Call(const MenuItemEventData& eventData)
        {

        }

        void Build(const MenuItemEventData& eventData)
        {
            String path;
            if (Platform::PickFolder(path, {}) == DialogResult::OK)
            {
                auto stat = FileSystem::GetFileStatus(path);
                if (!stat.exists)
                {
                    FileSystem::CreateDirectory(path);
                }
                AssetEditor::Export(path);
            }
        }

        void ReloadShaders(const MenuItemEventData& eventData)
        {
            for (AssetFile* assetFile : AssetEditor::GetAssetsOfType(GetTypeID<ShaderAsset>()))
            {
                if (ShaderAsset* shaderAsset = Assets::Get<ShaderAsset>(assetFile->uuid))
                {
                    if (shaderAsset->type != ShaderAssetType::Include)
                    {
                        Assets::Reload(assetFile->uuid);
                    }
                }
            }
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
            Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Editor Preferences...", .priority = 1000, .action = SettingsWindow::Open, .userData = GetTypeID<EditorPreferences>()});
            Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Project Settings...", .priority = 1010, .action = SettingsWindow::Open, .userData = GetTypeID<ProjectSettings>()});
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
                case DockPosition::TopRight: return topRightDockId;
                case DockPosition::BottomRight: return bottomRightDockId;
                case DockPosition::Bottom: return bottomDockId;
            }
            return U32_MAX;
        }

        u32 CreateWindow(const EditorWindowStorage& editorWindowStorage, VoidPtr userData)
        {
            TypeHandler* typeHandler = Registry::FindTypeById(editorWindowStorage.typeId);
            u32          windowId = idCounter;

            OpenWindowStorage openWindowStorage = OpenWindowStorage{
                .id = windowId,
                .instance = typeHandler->Cast<EditorWindow>(typeHandler->NewInstance()),
                .typeHandler = typeHandler
            };

            openWindowStorage.instance->Init(openWindowStorage.id, userData);

            openWindows.EmplaceBack(openWindowStorage);
            idCounter = idCounter + 1000;

            auto p = GetDockId(editorWindowStorage.dockPosition);
            if (p != U32_MAX)
            {
                ImGui::DockBuilderDockWindow(windowId, p);
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
                    openWindowStorage.typeHandler->Destroy(openWindowStorage.instance);
                    openWindows.Erase(openWindows.begin() + i, openWindows.begin() + i + 1);
                }
            }
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


        void InitDockSpace()
        {
            if (!dockInitialized)
            {
                dockInitialized = true;
                ImGui::DockBuilderReset(dockSpaceId);

                //create default windows
                centerSpaceId = dockSpaceId;
                topRightDockId = ImGui::DockBuilderSplitNode(centerSpaceId, ImGuiDir_Right, 0.15f, nullptr, &centerSpaceId);
                bottomRightDockId = ImGui::DockBuilderSplitNode(topRightDockId, ImGuiDir_Down, 0.50f, nullptr, &topRightDockId);
                bottomDockId = ImGui::DockBuilderSplitNode(centerSpaceId, ImGuiDir_Down, 0.20f, nullptr, &centerSpaceId);
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
                ImGui::StyleColor childBg(ImGuiCol_PopupBg, IM_COL32(28, 31, 33, 255));
                if (ImGui::BeginPopupModal("Save Content", &open, ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Text("Pending items to save");
                    {
                        ImGui::StyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));
                        ImGui::StyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));

                        f32 width = ImGui::GetContentRegionAvail().x - 5;
                        f32 height = ImGui::GetContentRegionAvail().y;
                        f32 buttonHeight = 25 * style.ScaleFactor;

                        if (ImGui::BeginChild(455343, ImVec2(width, height - buttonHeight), false))
                        {
                            if (ImGui::BeginTable("table-pending-to-save", 3, flags))
                            {
                                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 150.f * style.ScaleFactor);
                                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 300.f * style.ScaleFactor);
                                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 200.f * style.ScaleFactor);
                                ImGui::TableHeadersRow();

                                for (AssetFile* assetFile : updatedItems)
                                {
                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text("%s%s", assetFile->fileName.CStr(), assetFile->extension.CStr());
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::Text("%s", assetFile->path.CStr());

                                    ImGui::TableSetColumnIndex(2);
                                    if (!assetFile->active)
                                    {
                                        ImGui::Text("Deleted");
                                    }
                                    else if (assetFile->persistedVersion == 0)
                                    {
                                        ImGui::Text("Created");
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

                        ImGui::Spring(1.0f);

                        if (ImGui::Button("Save All"))
                        {
                            SaveAll(updatedItems);
                            forceClose = true;
                            Engine::Shutdown();
                        }

                        if (ImGui::Button("Don't Save"))
                        {
                            forceClose = true;
                            Engine::Shutdown();
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

        void SaveAll(Span<AssetFile*> assets)
        {
            AssetEditor::SaveAssets(assets);
        }

        void EditorUpdate(f64 deltaTime)
        {
            Array<std::function<void()>> callsMoved;
            {
                std::unique_lock lock(callsMutex);
                callsMoved = Traits::Move(calls);
            }

            for (const auto& func : callsMoved)
            {
                func();
            }

            sceneEditor->DoUpdate();

            ImGuiUpdate();

            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::CreateDockSpace(dockSpaceId);
            InitDockSpace();
            DrawOpenWindows();

            if (showImGuiDemo)
            {
                ImGui::ShowDemoWindow(&showImGuiDemo);
            }

            DrawMenu();
            ImGui::End();

            ProjectUpdate();
        }

        void OnEditorShutdownRequest(bool* canClose)
        {
            if (forceClose) return;

            if (sceneEditor->IsSimulating())
            {
                sceneEditor->StopSimulation();
                *canClose = false;
                return;
            }

            updatedItems.Clear();
            AssetEditor::GetUpdatedAssets(updatedItems);

            if (!updatedItems.Empty())
            {
                *canClose = false;
                shouldOpenPopup = true;
            }
        }
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


    EditorTransaction* Editor::CreateTransaction()
    {
        redoActions.Clear();
        return undoActions.EmplaceBack(MakeShared<EditorTransaction>()).Get();
    }

    void Editor::AddMenuItem(const MenuItemCreation& menuItem)
    {
        menuContext.AddMenuItem(menuItem);
    }

    SceneEditor& Editor::GetSceneEditor()
    {
        return *sceneEditor;
    }

    String Editor::CreateProject(StringView newProjectPath, StringView projectName)
    {
        String fullProjectPath = Path::Join(newProjectPath, projectName);
        String assetsPath = Path::Join(fullProjectPath, "Assets");
        String tempPath = Path::Join(fullProjectPath, "Temp");
        String settingsPath = Path::Join(fullProjectPath, "Settings");
        String projectFilePath = Path::Join(fullProjectPath, projectName, SK_PROJECT_EXTENSION);

        FileSystem::CreateDirectory(assetsPath);
        FileSystem::CreateDirectory(tempPath);
        FileSystem::CreateDirectory(settingsPath);

        JsonArchiveWriter jsonAssetWriter;
        ArchiveValue      object = jsonAssetWriter.CreateObject();
        jsonAssetWriter.AddToObject(object, "engineVersion", jsonAssetWriter.StringValue(SK_VERSION));
        FileSystem::SaveFileAsString(projectFilePath, JsonArchiveWriter::Stringify(object));

        return projectFilePath;
    }

    void Editor::ExecuteOnMainThread(std::function<void()> func)
    {
        std::unique_lock lock(callsMutex);
        calls.EmplaceBack(func);
    }

    void Editor::Init(StringView currentProjectFile)
    {
        projectFile = currentProjectFile;

        if (Path::Extension(projectFile) != SK_PROJECT_EXTENSION)
        {
            return;
        }

        ShaderManagerInit();

        Registry::Type<EditorWindow>();
        Registry::Type<EditorPreferences>();

        RegisterAssetTypes();
        InitEditorAction();
        RegisterFieldRenderers();
        RegistrySceneEditorTypes();

        Registry::Type<ProjectBrowserWindow>();
        Registry::Type<TextureViewWindow>();
        Registry::Type<SceneTreeWindow>();
        Registry::Type<PropertiesWindow>();
        Registry::Type<SceneViewWindow>();
        Registry::Type<SettingsWindow>();


        Event::Bind<OnInit, &InitEditor>();
        Event::Bind<OnUpdate, &EditorUpdate>();
        Event::Bind<OnShutdown, &Shutdown>();
        Event::Bind<OnShutdownRequest, &OnEditorShutdownRequest>();

        CreateMenuItems();
    }
}
