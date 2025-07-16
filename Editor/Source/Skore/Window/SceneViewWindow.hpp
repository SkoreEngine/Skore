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

#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Graphics/BasicSceneRenderer.hpp"
#include "Skore/Scene/SceneViewRenderer.hpp"
#include "Skore/Utils/EntityPicker.hpp"
#include "Skore/Utils/FreeViewCamera.hpp"


namespace Skore
{
	class SceneViewWindow : public EditorWindow
	{
	public:
		SK_CLASS(SceneViewWindow, EditorWindow);

		~SceneViewWindow() override;

		void Init(u32 id, VoidPtr userData) override;
		void Draw(u32 id, bool& open) override;


		static void RegisterType(NativeReflectType<SceneViewWindow>& type);

		//menu items only work with hotkeys on SceneView
		static void AddMenuItem(const MenuItemCreation& menuItem);

	private:
		u32            guizmoOperation{1};
		u8             guizmoMode{0};
		bool           guizmoSnapEnabled = false;
		Vec3           guizmoSnap{1.0f, 1.0f, 1.0f};
		bool           windowStartedSimulation{};
		bool           movingScene{};
		FreeViewCamera freeViewCamera{};
		bool           usingGuizmo{};
		Transform      gizmoInitialTransform = {};
		Mat4           view{};
		Mat4           projection{};
		bool           view2d = false;
		f32            cameraFov = 60.f;
		f32            aspectRatio = 1.f;

		Entity* previewEntity = nullptr;

		Extent            sceneExtent;
		GPUTexture*       sceneTexture = nullptr;
		GPURenderPass*    sceneRenderPass = nullptr;
		SceneViewRenderer sceneViewRenderer = {};

		SceneRendererViewport sceneRendererViewport = {};
		EntityPicker          entityPicker = {};

		void RecordRenderCommands(GPUCommandBuffer* cmd);

		static void OpenSceneView(const MenuItemEventData& eventData);
		static void DuplicateSceneEntity(const MenuItemEventData& eventData);
		static void DeleteSceneEntity(const MenuItemEventData& eventData);
		static bool CheckSelectedEntity(const MenuItemEventData& eventData);


		static MenuItemContext menuItemContext;
	};
}
