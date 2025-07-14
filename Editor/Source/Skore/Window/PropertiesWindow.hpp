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
#include "Skore/Core/Object.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	class SceneEditor;
	class ReflectField;
	struct MenuItemEventData;
	class Entity;

	class PropertiesWindow : public EditorWindow
	{
	public:
		SK_CLASS(PropertiesWindow, EditorWindow);

		PropertiesWindow();
		~PropertiesWindow() override;
		void Draw(u32 id, bool& open) override;

		static void RegisterType(NativeReflectType<PropertiesWindow>& type);

	private:
		String stringCache{};
		RID    selectedEntity{};
		bool   renamingFocus{};
		String renamingCache{};
		RID    renamingEntity{};
		String searchComponentString{};
		RID    selectedComponent = {};
		u32    selectedComponentIndex = U32_MAX;
		RID    selectedAsset = {};

		Entity* selectedDebugEntity = nullptr;

		void ClearSelection();

		static void OpenProperties(const MenuItemEventData& eventData);

		void DrawEntity(u32 id, SceneEditor* sceneEditor, RID entity);
		void DrawDebugEntity(u32 id, SceneEditor* sceneEditor, Entity* entity);
		void DrawAsset(u32 id, RID asset);

		void EntityDebugSelection(u32 workspaceId, Entity* entity);
		void EntityDebugDeselection(u32 workspaceId, Entity* entity);

		void EntitySelection(u32 workspaceId, RID entityId);
		void EntityDeselection(u32 workspaceId, RID entityId);

		void AssetSelection(u32 workspaceId, RID assetId);
	};
}
