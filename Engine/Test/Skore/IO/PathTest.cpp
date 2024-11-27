
#include <doctest.h>
#include "Skore/IO/Path.hpp"

using namespace Skore;

namespace
{
    TEST_CASE("IO::PathBasics")
    {
        String check{};
        check += "C:";
        check += SK_PATH_SEPARATOR;
        check += "Folder1";
        check += SK_PATH_SEPARATOR;
        check += "Folder2";
        check += SK_PATH_SEPARATOR;
        check += "Folder3";
        check += SK_PATH_SEPARATOR;
        check += "Folder4";

        String parent = Path::Join("C:/", "Folder1/", "/Folder2", "Folder3/Folder4");
        CHECK(!parent.Empty());
        CHECK(check == parent);

        String file = Path::Join(parent, "Leaf.exe");

        check += SK_PATH_SEPARATOR;
        check += "Leaf.exe";
        CHECK(file == check);

        CHECK(!file.Empty());
        CHECK(Path::Extension(file) == ".exe");
        CHECK(Path::Name(file) == "Leaf");
        CHECK(Path::Parent(file) == parent);
    }
}