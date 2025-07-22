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
#include "Skore/Core/String.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	struct SettingsItem
	{
		String        label;
		RID           rid;
		ResourceType* type;

		Array<std::shared_ptr<SettingsItem>> children;
	};

	class SettingsWindow : public EditorWindow
	{
	public:
		SK_CLASS(SettingsWindow, EditorWindow);

		void Init(u32 id, VoidPtr userData) override;
		void Draw(u32 id, bool& open) override;


		static void Open(TypeID group);
		static void RegisterType(NativeReflectType<SettingsWindow>& type);

	private:
		String title;
		TypeID settingsType = 0;
		String searchText;
		RID    selectedItem = {};

		Array<std::shared_ptr<SettingsItem>> rootItems;

		void DrawTree();
		void DrawItem(const SettingsItem& settingsItem, u32 level);
		void DrawSelected() const;

		static void OpenAction(const MenuItemEventData& eventData);
	};
}
