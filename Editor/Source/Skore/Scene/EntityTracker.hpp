#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
	class Entity;
}

namespace Skore::EntityTracker
{
	SK_API void Init();
	SK_API void Shutdown();
	SK_API bool IsAlive(Entity* entity);
}
