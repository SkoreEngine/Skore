#pragma once
#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Editor/MenuItem.hpp"
#include "Skore/Editor/Action/EditorAction.hpp"
#include "Skore/Editor/Scene/SceneEditor.hpp"
#include "Skore/Graphics/FreeViewCamera.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/DefaultRenderPipeline/DefaultRenderPipeline.hpp"

namespace Skore
{
    class SceneViewWindow : public EditorWindow
    {
    public:
        SK_BASE_TYPES(EditorWindow);

        SceneViewWindow();
        ~SceneViewWindow() override;

        void Draw(u32 id, bool& open) override;

        //menu items only work with hotkeys on SceneView
        static void AddMenuItem(const MenuItemCreation& menuItem);

        static void RegisterType(NativeTypeHandler<SceneViewWindow>& type);

    private:
        SceneEditor&    sceneEditor;
        u32             guizmoOperation{1};
        bool            windowStartedSimulation{};
        bool            movingScene{};
        RenderGraph*    renderGraph{};
        FreeViewCamera  freeViewCamera{};
        CameraData      cameraData = {};
        DefaultRenderPipeline defaultRenderPipeline{};
        bool    renderDirty = false;

        bool      usingGuizmo{};
        Transform gizmoInitialTransform = {};

        void RecordRenderCommands(RenderCommands& cmd, f64 deltaTime);

        static void OpenSceneView(const MenuItemEventData& eventData);

        static void DuplicateSceneObject(const MenuItemEventData& eventData);
        static void DeleteSceneObject(const MenuItemEventData& eventData);
        static bool CheckSelectedObject(const MenuItemEventData& eventData);


        static MenuItemContext menuItemContext;
    };
}
