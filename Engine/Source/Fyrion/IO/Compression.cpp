#include "Compression.hpp"

#include "lz4.h"
#include "zstd.h"
#include "Fyrion/Core/Algorithm.hpp"

//based on https://github.com/godotengine/godot/blob/e65a23762b36b564eb94672031f37fdadba72333/core/io/compression.cpp

namespace Fyrion
{
    constexpr int zstdLlevel = 3;

    usize Compression::Compress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode)
    {
        switch (mode)
        {
            case CompressionMode::ZSTD:
            {
                return ZSTD_compress(dest, descSize, src, srcSize, zstdLlevel);
            }
            case CompressionMode::LZ4:
            {
                return LZ4_compress_default(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest),
                                            srcSize,
                                            descSize);
            }
            default:
            {
                MemCopy(dest, src, srcSize);
                return srcSize;
            }
        }
    }

    usize Compression::GetMaxCompressedBufferSize(usize srcSize, CompressionMode mode)
    {
        switch (mode)
        {
            case CompressionMode::ZSTD:
            {
                return ZSTD_compressBound(srcSize);
            }
            case CompressionMode::LZ4:
            {
                return LZ4_compressBound(srcSize);
            }
            default:
            {
                return srcSize;
            }
        }
    }

    usize Compression::Decompress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode)
    {
        switch (mode)
        {
            case CompressionMode::ZSTD:
            {
                return ZSTD_decompress(dest, descSize, src, srcSize);
            }
            case CompressionMode::LZ4:
            {
                return LZ4_decompress_safe(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest), srcSize, descSize);
            }
            default:
            {
                MemCopy(dest, src, srcSize);
                return srcSize;
            }
        }
    }
}
