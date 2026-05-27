#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	struct MenuItemEventData;
	class ResourceType;

	class ResourceDebuggerWindow : public EditorWindow
	{
	public:
		SK_CLASS(ResourceDebuggerWindow, EditorWindow);

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void RegisterType(NativeReflectType<ResourceDebuggerWindow>& type);
		static void Open(const MenuItemEventData& eventData);
		static void InspectResource(RID rid);

		void NavigateToInstance(RID rid);

	private:
		void DrawTypesTab(u32 id);
		void DrawInstanceTab(u32 id);
		void DrawInstanceFields(RID rid);
		void SelectTypeInTab(ResourceType* type);

		String        searchFilter;
		ResourceType* selectedType = nullptr;
		RID           selectedInstance{};
		Array<RID>    instanceHistory;
		bool          switchToInstanceTab = false;
		bool          switchToTypesTab = false;
	};
}
