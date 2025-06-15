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
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/World/WorldEditor.hpp"

namespace Skore
{
	class EntityTreeWindow : public EditorWindow
	{
	public:
		SK_CLASS(EntityTreeWindow, EditorWindow);


		void Init(u32 id, VoidPtr userData) override;
		void Draw(u32 id, bool& open) override;

		static void AddMenuItem(const MenuItemCreation& menuItem);
		static void AddSceneEntity(const MenuItemEventData& eventData);
		static void AddSceneEntityFromAsset(const MenuItemEventData& eventData);
		static void AddComponent(const MenuItemEventData& eventData);
		static void RenameSceneEntity(const MenuItemEventData& eventData);
		static void DuplicateSceneEntity(const MenuItemEventData& eventData);
		static void DeleteSceneEntity(const MenuItemEventData& eventData);
		static bool CheckSelectedEntity(const MenuItemEventData& eventData);
		static bool CheckReadOnly(const MenuItemEventData& eventData);
		static void ShowWorldEntity(const MenuItemEventData& eventData);
		static bool IsShowWorldEntitySelected(const MenuItemEventData& eventData);

		static void OpenEntityTree(const MenuItemEventData& eventData);
		static void RegisterType(NativeReflectType<EntityTreeWindow>& type);

	private:
		String searchEntity{};
		String stringCache{};

		bool   renamingFocus{};
		bool   renamingSelected{};
		String renamingStringCache{};
		f32    iconSize = {};

		bool    showWorldEntity = false;

		static MenuItemContext menuItemContext;

		void DrawRIDEntity(WorldEditor* worldEditor, RID entity, bool& entitySelected);
		void DrawEntity(WorldEditor* worldEditor, Entity* entity);
	};
}
