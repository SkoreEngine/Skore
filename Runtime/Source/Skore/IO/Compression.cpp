#include "Skore/IO/Compression.hpp"

//#include "lz4.h"
#include "zstd.h"
#include "Skore/Core/Algorithm.hpp"


namespace Skore
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
				// case CompressionMode::LZ4:
				// {
				//     return LZ4_compress_default(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest),
				//                                 srcSize,
				//                                 descSize);
				// }
		}

		return 0;
	}

	usize Compression::GetMaxCompressedBufferSize(usize srcSize, CompressionMode mode)
	{
		switch (mode)
		{
			case CompressionMode::ZSTD:
			{
				return ZSTD_compressBound(srcSize);
			}
				// case CompressionMode::LZ4:
				// {
				//     return LZ4_compressBound(srcSize);
				// }
		}

		return 0;
	}

	usize Compression::Decompress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode)
	{
		switch (mode)
		{
			case CompressionMode::ZSTD:
			{
				return ZSTD_decompress(dest, descSize, src, srcSize);
			}
				// case CompressionMode::LZ4:
				// {
				//     return LZ4_decompress_safe(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest), srcSize, descSize);
				// }
		}

		return 0;
	}

	usize Compression::GetMaxDecompressedBufferSize(const u8* src, usize srcSize, CompressionMode mode)
	{
		switch (mode)
		{
			case CompressionMode::ZSTD:
			{
				return ZSTD_getFrameContentSize(src, srcSize);
			}
		}
		return 0;
	}
}