#pragma once
#include "Skore/Common.hpp"


namespace Skore
{
    enum class CompressionMode
    {
        None,
        ZSTD,
        LZ4
    };
}

namespace Skore::Compression
{
    SK_API usize Compress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode);
    SK_API usize GetMaxCompressedBufferSize(usize srcSize, CompressionMode mode);
    SK_API usize Decompress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode);
}