#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"

#include "Skore/Utils/EntityPicker.hpp"
#include "Skore/Utils/FreeViewCamera.hpp"



namespace Skore
{
	class RenderPipelineContext;

	class SceneViewWindow : public EditorWindow
	{
	public:
		SK_CLASS(SceneViewWindow, EditorWindow);

		enum
		{
			ViewType_3D = 0,
			ViewType_2D = 1,
		};

		using ViewType = u8;

		SceneViewWindow() = default;
		~SceneViewWindow() override;

		void Init(u32 id, VoidPtr userData) override;
		void Draw(u32 id, bool& open) override;
		void Render(GPUCommandBuffer* cmd) override;

		static void ViewEntity(Entity* entity);
		static void ViewEntity(RID entity);

		static void RegisterType(NativeReflectType<SceneViewWindow>& type);

		//menu items only work with hotkeys on SceneView
		static void AddMenuItem(const MenuItemCreation& menuItem);


		SceneEditor* GetSceneEditor() const { return sceneEditor; }
		friend struct SceneViewPipelinePass;

	private:
		u32            guizmoOperation{1};
		u8             guizmoMode{0};
		bool           guizmoSnapEnabled = false;
		Vec3           guizmoSnap{1.0f, 1.0f, 1.0f};
		bool           windowStartedSimulation{};
		bool           movingScene{};
		FreeViewCamera freeViewCamera{};
		bool           usingGuizmo{};
		ViewType       viewType = ViewType_3D;
		bool           drawIcons = true;
		f32            cameraFov = 60.f;
		f32            aspectRatio = 1.f;

		SceneEditor* sceneEditor{};
		bool         ctrlDown = false;
		bool         hovered = false;
		Vec2			   mousePosRelativeToWindow;
		Vec2         cursor;
		Vec2         initCursor;
		Vec2         size;
		f32          iconSize = 0;

		bool drawGrid = true;
		bool drawSelectionOutline = true;
		bool drawDebugPhysics = true;
		bool showAllPhysicsShapes = false;
		bool lockCameraFrustum = false;
		bool drawMeshAABB = false;
		bool drawNavMesh = false;
		bool pathTracerEnabled = false;
		String selectedTextureToShow = "";

		Vec2 textPos = {100, 100};
		f32  textSize = 14;

		GPUTexture* logoTexture = nullptr;

		Entity* previewEntity = nullptr;
		RID     previewEntityRID = {};

		EntityPicker entityPicker = {};

		RenderPipelineContext* renderPipelineContext = nullptr;


		static void OpenSceneView(const MenuItemEventData& eventData);
		static void DuplicateSceneEntity(const MenuItemEventData& eventData);
		static void DeleteSceneEntity(const MenuItemEventData& eventData);
		static bool CheckSelectedEntity(const MenuItemEventData& eventData);


		static MenuItemContext menuItemContext;

		void Draw3DViewport(u32 id);
		void Draw2DViewport(u32 id);
	};
}
