#pragma once
#include "Skore/Core/Event.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	using OnInit = EventType<"Skore::OnInit"_h, void()>;
	using OnBeginFrame = EventType<"Skore::OnBeginFrame"_h, void()>;
	using OnUpdate = EventType<"Skore::OnUpdate"_h, void()>;
	using OnEndFrame = EventType<"Skore::OnEndFrame"_h, void()>;
	using OnShutdown = EventType<"Skore::OnShutdown"_h, void()>;
	using OnPluginReloaded = EventType<"Skore::OnPluginReloaded"_h, void()>;
	using OnResourceTypeReloaded = EventType<"Skore::OnResourceTypeReloaded"_h, void(TypeID typeId)>;
	using OnShutdownRequest = EventType<"Skore::OnShutdownRequest"_h, void(bool* canClose)>;
	using OnDropFileCallback = EventType<"Skore::OnDropFileCallback"_h, void(StringView path)>;
	using OnSettingsLoaded = EventType<"Skore::OnSettingsLoaded"_h, void()>;
}
