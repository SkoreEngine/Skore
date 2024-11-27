
#include <doctest.h>
#include "Skore/Core/StringView.hpp"

using namespace Skore;

namespace
{
    TEST_CASE("Core::Algorithm::SearchSubString")
    {
        {
            usize pos = SearchSubString(StringView{"abcde"}, StringView{"bc"});
            CHECK(pos == 1);
        }

        {
            usize pos = SearchSubString(StringView{"abcde"}, StringView{"bf"});
            CHECK(pos == nPos);
        }

        {
            CHECK(Contains(StringView{"abcde"}, StringView{"bc"}));
        }

    }
}