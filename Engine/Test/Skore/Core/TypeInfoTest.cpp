
#include <doctest.h>
#include "Skore/Core/TypeInfo.hpp"

using namespace Skore;


namespace
{

    TEST_CASE("Core::GetSimpleName")
    {
        {
            StringView str = GetSimpleName("Core::Test::TestName");
            CHECK(str == "TestName");
        }

        {
            StringView str = GetSimpleName("TestName");
            CHECK(str == "TestName");
        }
    }
}
