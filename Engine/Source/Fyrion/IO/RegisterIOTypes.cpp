#include "Asset.hpp"
#include "Compression.hpp"
#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{
    void RegisterIOTypes()
    {
        auto compressionMode = Registry::Type<CompressionMode>();
        compressionMode.Value<CompressionMode::None>("None");
        compressionMode.Value<CompressionMode::ZSTD>("ZSTD");
        compressionMode.Value<CompressionMode::LZ4>("LZ4");

        Registry::Type<Asset>();
    }
}
