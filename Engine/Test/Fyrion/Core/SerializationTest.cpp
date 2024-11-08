#include <doctest.h>

#include "Fyrion/Core/Array.hpp"
#include "Fyrion/Core/Serialization.hpp"

using namespace Fyrion;

namespace
{
    TEST_CASE("Core::Serialization")
    {
        String json;
        {
            JsonArchiveWriter archiveWriter;
            ArchiveValue object = archiveWriter.CreateObject();

            archiveWriter.AddToObject(object, "intValue", archiveWriter.IntValue(456546564));
            archiveWriter.AddToObject(object, "strValue", archiveWriter.StringValue("teststr"));

            json = JsonArchiveWriter::Stringify(object, false, true);
        }
        CHECK(!json.Empty());

        {
            JsonArchiveReader reader(json, true);

            ArchiveValue object = reader.GetRoot();
            CHECK(reader.IntValue(reader.GetObjectValue(object, "intValue")) == 456546564);
            CHECK(reader.StringValue(reader.GetObjectValue(object, "strValue")) == "teststr");
        }

    }
}
