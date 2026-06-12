#include "SystemInterfaceSkore.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Platform/Platform.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore.RmlUi");
	}

	SystemInterfaceSkore::SystemInterfaceSkore() : startTime(Platform::GetTime()) {}

	double SystemInterfaceSkore::GetElapsedTime()
	{
		return static_cast<double>(Platform::GetTime() - startTime) / 1.0e9;
	}

	bool SystemInterfaceSkore::LogMessage(Rml::Log::Type type, const Rml::String& message)
	{
		switch (type)
		{
			case Rml::Log::LT_ERROR:
			case Rml::Log::LT_ASSERT:
				logger.Error("{}", message.c_str());
				break;
			case Rml::Log::LT_WARNING:
				logger.Warn("{}", message.c_str());
				break;
			default:
				logger.Info("{}", message.c_str());
				break;
		}
		return true;
	}
}
