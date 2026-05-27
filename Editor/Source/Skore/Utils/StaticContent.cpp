#include "Skore/Utils/StaticContent.hpp"

#include <optional>

#include "stb_image.h"
#include "cmrc/cmrc.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

CMRC_DECLARE(StaticContent);


namespace Skore::StaticContent
{
	static Logger& logger = Logger::GetLogger("Skore::StaticContent");

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

	Image GetImage(StringView path)
	{
		Array<u8> file = GetBinaryFile(path);

		Image image{};
		i32   imageChannels{};

		stbi_uc* bytes = stbi_load_from_memory(file.Data(), file.Size(), &image.width, &image.height, &imageChannels, 4);
		if (!bytes)
		{
			logger.Error("Failed to decode image {}", path);
			return {};
		}

		const usize size = static_cast<usize>(image.width * image.height * 4);
		image.pixels.Resize(size);
		memcpy(image.pixels.Data(), bytes, size);

		stbi_image_free(bytes);

		return image;
	}

	void SaveFilesToDirectory(StringView path, StringView directory)
	{
		auto fs = cmrc::StaticContent::get_filesystem();

		if (!fs.is_directory(path.CStr()))
		{
			logger.Error("{} is not a directory", path);
			return;
		}

		FileSystem::Remove(directory);
		FileSystem::CreateDirectory(directory);

		Queue<std::string> pending;
		std::string current = path.CStr();

		while (!current.empty())
		{
			FileSystem::CreateDirectory(Path::Join(directory, current.c_str()));

			for (cmrc::directory_entry dir: fs.iterate_directory(current))
			{
				std::string filePath = current + "/" + dir.filename();

				if (dir.is_file())
				{
					String pathSavedTo = Path::Join(directory, filePath.c_str());
					logger.Debug("found file {}, saving on {} ", filePath, pathSavedTo);

					auto file = fs.open(filePath);
					FileSystem::SaveFileAsByteArray(pathSavedTo, Span(const_cast<u8*>(file.begin()), const_cast<u8*>(file.end())));
				}

				if (dir.is_directory())
				{
					pending.Enqueue(filePath);
				}
			}

			current = {};

			if (!pending.IsEmpty())
			{
				current = pending.Dequeue();
			}
		}
	}
}
