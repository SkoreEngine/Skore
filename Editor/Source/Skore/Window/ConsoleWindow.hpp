#pragma once
#include "imgui.h"
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Sinks.hpp"

namespace Skore
{
	struct MenuItemEventData;

	class ConsoleWindow : public EditorWindow
	{
	public:
		SK_CLASS(ConsoleWindow, EditorWindow);

		void Draw(u32 id, bool& open) override;

		static void RegisterType(NativeReflectType<ConsoleWindow>& type);
		static void OpenHistoryWindow(const MenuItemEventData& eventData);

	private:
		bool              showTrace = false;
		bool              showDebug = false;
		bool              showInfo = true;
		bool              showWarn = true;
		bool              showError = true;
		bool              showCritical = true;
		bool              shouldScrollToBottom = true;
		bool              collapse = true;
		ImGuiTextFilter   filter;
		Array<LogMessage> logs;
		Array<CollapsedLogMessage> collapsedLogs;
		u64               version = 0;
	};
}
