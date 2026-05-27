#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	struct SettingsEntry
	{
		RID           rid;
		ResourceType* type  = nullptr;
		i32           order = 0;
	};

	struct SettingsItem
	{
		String               label;
		Array<SettingsEntry> entries;
		i32                  order = 0;

		Array<std::shared_ptr<SettingsItem>> children;
	};

	class SettingsWindow : public EditorWindow
	{
	public:
		SK_CLASS(SettingsWindow, EditorWindow);

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;


		static void Open(TypeID group);
		static void RegisterType(NativeReflectType<SettingsWindow>& type);

	private:
		String              title;
		TypeID              settingsType = 0;
		String              searchText;
		const SettingsItem* selectedItem = nullptr;

		Array<std::shared_ptr<SettingsItem>> rootItems;

		void DrawTree();
		void DrawItem(const SettingsItem& settingsItem, u32 level);
		void DrawSelected() const;
		void DrawCollisionMatrix(RID rid) const;

		static void OpenAction(const MenuItemEventData& eventData);
	};
}
