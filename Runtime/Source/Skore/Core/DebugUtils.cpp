#include "Skore/Core/DebugUtils.hpp"

#include "Skore/Core/Logger.hpp"

namespace Skore
{


	void ScopedTimer::Check(StringView message) const
	{
		logger.Debug("check: {} {} ms", message, timer.Diff());
	}

	ScopedTimer::~ScopedTimer()
	{
		logger.Info(" {} {} ms", message, timer.Diff());
	}
}