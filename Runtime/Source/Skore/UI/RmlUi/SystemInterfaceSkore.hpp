#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include "Skore/Common.hpp"

namespace Skore
{
	class SystemInterfaceSkore : public Rml::SystemInterface
	{
	public:
		SystemInterfaceSkore();

		double GetElapsedTime() override;
		bool   LogMessage(Rml::Log::Type type, const Rml::String& message) override;
		void   ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
		void   DeactivateKeyboard() override;

	private:
		u64 startTime = 0;
	};
}
