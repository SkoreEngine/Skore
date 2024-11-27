#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Sinks.hpp"

using namespace Skore;

int main(int argc, char** argv)
{
    AllocatorOptions flags = AllocatorOptions_DetectMemoryLeaks;

    //flags |= AllocatorOptions_CaptureStackTrace;

    MemoryGlobals::SetOptions(flags);

    StdOutSink sink{};
    Logger::RegisterSink(sink);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    context.setOption("no-breaks", true);

    i32 res = context.run();

    Logger::Reset();
    Event::Reset();

    return res;
}