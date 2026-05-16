#pragma once

#include "Skore/EditorCommon.hpp"

namespace Skore
{
	struct MenuItemEventData;

	class HistoryWindow : public EditorWindow
	{
	public:
		SK_CLASS(HistoryWindow, EditorWindow);

		void Draw(u32 id, bool& open) override;

		static void RegisterType(NativeReflectType<HistoryWindow>& type);
		static void OpenHistoryWindow(const MenuItemEventData& eventData);

	private:
		bool m_shouldAutoScroll = false;
	};
}