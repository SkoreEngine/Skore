#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Scene/Scene.hpp"

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

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;
		void        Render(GPUCommandBuffer* cmd) override;

		static void ViewEntity(Entity* entity);
		static void ViewEntity(RID entity);

		static void RegisterType(NativeReflectType<SceneViewWindow>& type);

		//menu items only work with hotkeys on SceneView
		static void AddMenuItem(const MenuItemCreation& menuItem);


		SceneEditor* GetSceneEditor() const { return sceneEditor; }
		friend struct SceneViewPipelinePass;

	private:
		//world transform of an entity captured when the gizmo drag starts,
		//cumulative gizmo deltas are applied on top of these values
		struct GizmoTarget
		{
			RID  entity;
			Vec3 worldPos;
			Quat worldRot;
			Vec3 worldScale;
		};

		u32            guizmoOperation{1};
		u8             guizmoMode{0};
		bool           guizmoSnapEnabled = false;
		Vec3           guizmoSnap{1.0f, 1.0f, 1.0f};
		bool           windowStartedSimulation{};
		bool           movingScene{};
		FreeViewCamera freeViewCamera{};
		bool           usingGuizmo{};

		Array<GizmoTarget> gizmoTargets;
		Mat4               gizmoMatrix{1.0};
		Vec3               gizmoStartPos{};
		Quat               gizmoStartRot{0, 0, 0, 1};
		Vec3               gizmoStartScale{1.0f, 1.0f, 1.0f};
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
		Scene emptyScene;

		void Draw3DViewport(u32 id);
		void Draw2DViewport(u32 id);
	};
}
