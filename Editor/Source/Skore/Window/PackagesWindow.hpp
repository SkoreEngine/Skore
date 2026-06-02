#pragma once

#include "Skore/EditorCommon.hpp"

namespace Skore
{
	struct MenuItemEventData;

	class PackagesWindow : public EditorWindow
	{
	public:
		SK_CLASS(PackagesWindow, EditorWindow);

		const char* GetTitle() const override;
		void        Draw(bool& open) override;

		static void Open(const MenuItemEventData& eventData);
		static void RegisterType(NativeReflectType<PackagesWindow>& type);
	};
}
