#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"


namespace Skore::Launcher
{
    SK_API void   Init();
    SK_API void   Shutdown();
    SK_API String GetProject();
}
