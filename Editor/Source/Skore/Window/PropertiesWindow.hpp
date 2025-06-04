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
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	class ReflectField;
	class Component;
	class SceneEditor;
}

namespace Skore
{
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
		String        stringCache{};
		UUID          selectedEntity{};
		bool          renamingFocus{};
		String        renamingCache{};
		Entity*       renamingEntity{};
		String        searchComponentString{};
		Component*    selectedComponent = {};

		//refactor this.
		ReflectField* currentField = {};
		Entity*       currentEntity = {};

		AssetFileOld* selectedAsset = {};

		void ClearSelection();

		static void OpenProperties(const MenuItemEventData& eventData);

		void DrawSceneEntity(u32 id, SceneEditor* sceneEditor, Entity* entity);
		void DrawAsset(u32 id, AssetFileOld* assetFile);

		void EntitySelection(u32 workspaceId, UUID entityId);
		void EntityDeselection(u32 workspaceId, UUID entityId);
		void AssetSelection(AssetFileOld* assetFile);
	};
}
