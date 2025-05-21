// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "StaticContent.hpp"

#include "Span.hpp"
#include "stb_image.h"
#include "cmrc/cmrc.hpp"
#include "Skore/Graphics/Graphics.hpp"

CMRC_DECLARE(StaticContent);


namespace Skore::StaticContent
{
	Array<u8> GetBinaryFile(StringView path)
	{
		auto fs = cmrc::StaticContent::get_filesystem();
		auto file = fs.open(path.CStr());
		return {file.begin(), file.end()};
	}

	String GetTextFile(StringView path)
	{
		auto fs = cmrc::StaticContent::get_filesystem();
		auto file = fs.open(path.CStr());

		//workaround to remove \r from the strings
		std::string str((const char*)file.begin(), file.size());
		std::erase(str, '\r');
		return {str.c_str(), str.size()};
	}

	GPUTexture* GetTexture(StringView path)
	{
		Array<u8> image = GetBinaryFile(path);

		i32 imageWidth{};
		i32 imageHeight{};
		i32 imageChannels{};

		stbi_uc* bytes = stbi_load_from_memory(image.Data(), image.Size(), &imageWidth, &imageHeight, &imageChannels, 4);

		GPUTexture* texture = Graphics::CreateTexture({
			.extent = {static_cast<u32>(imageWidth), static_cast<u32>(imageHeight), 1},
			.format = TextureFormat::R8G8B8A8_UNORM,
			.usage = ResourceUsage::CopyDest | ResourceUsage::ShaderResource
		});

		Graphics::UploadTextureData(TextureDataInfo{
			.texture = texture,
			.data = bytes,
			.size = static_cast<usize>(imageWidth * imageHeight * 4),
		});

		stbi_image_free(bytes);

		return texture;
	}
}
