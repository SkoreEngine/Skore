#include "Asset.hpp"
#include "Compression.hpp"
#include "Skore/Core/Registry.hpp"

namespace Skore
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
