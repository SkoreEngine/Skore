#pragma once
#include "Skore/Common.hpp"


namespace Skore
{
	enum class CompressionMode
	{
		None,
		ZSTD
	};

	constexpr i32 CompressionDefaultLevel = 3;
	constexpr i32 CompressionMaxLevel = 19;
}

namespace Skore::Compression
{
	SK_API usize Compress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode, i32 level = CompressionDefaultLevel);
	SK_API usize GetMaxCompressedBufferSize(usize srcSize, CompressionMode mode);
	SK_API usize Decompress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode);
	SK_API usize GetMaxDecompressedBufferSize(const u8* src, usize srcSize, CompressionMode mode);
}
