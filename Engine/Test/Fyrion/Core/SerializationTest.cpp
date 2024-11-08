#include <doctest.h>

#include "Fyrion/Core/Array.hpp"
#include "Fyrion/Core/Serialization.hpp"

using namespace Fyrion;

namespace
{
    TEST_CASE("Core::BinarySerialization")
    {
        Array<u8> bytes;
        {
            BinaryArchiveWriter archiveWriter;
            ArchiveValue object = archiveWriter.CreateObject();

            archiveWriter.AddToObject(object, "intValue", archiveWriter.IntValue(456546564));
            archiveWriter.AddToObject(object, "strValue", archiveWriter.StringValue("blah"));

            bytes = BinaryArchiveWriter::GetBytes(object);

            BinaryArchiveWriter::Free(object);
        }
        CHECK(!bytes.Empty());

        {
            BinaryArchiveReader reader;

            ArchiveValue object = BinaryArchiveReader::Open(bytes);


        }



    }
}
