#pragma once
#include "Fyrion/Common.hpp"


namespace Fyrion
{
    enum class CompressionMode
    {
        None,
        ZSTD,
        LZ4
    };
}

namespace Fyrion::Compression
{
    FY_API usize Compress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode);
    FY_API usize GetMaxCompressedBufferSize(usize srcSize, CompressionMode mode);
    FY_API usize Decompress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode);
}