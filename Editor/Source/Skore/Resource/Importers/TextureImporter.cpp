#include "Skore/Resource/Importers/TextureImporter.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/Path.hpp"

#include <stb_image.h>

#include "Skore/Editor.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/Compression.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::TextureImporter");

	struct TextureImporter : ResourceAssetImporter
	{
		SK_CLASS(TextureImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"};
		}

		bool ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) override
		{
			TextureImportSettings importSettings = settings
				                                       ? *static_cast<const TextureImportSettings*>(settings)
				                                       : TextureImportSettings{
				                                       };

			ImportTexture(directory, importSettings, path, scope);
			return true;
		}
	};

	void RegisterTextureImporter()
	{
		Reflection::Type<TextureImporter>();
	}

	static void ScaleAlphaToCoverage(ByteBuffer& mips, u32 width, u32 height, u32 mipLevels, f32 cutoff)
	{
		auto computeCoverage = [](const u8* pixels, usize count, f32 cutoff, f32 scale) -> f64
		{
			usize covered = 0;
			for (usize i = 0; i < count; ++i)
			{
				f32 a = (static_cast<f32>(pixels[i * 4 + 3]) / 255.0f) * scale;
				if (a >= cutoff) ++covered;
			}
			return count > 0 ? static_cast<f64>(covered) / static_cast<f64>(count) : 0.0;
		};

		u8* base = mips.begin();
		f64 refCoverage = computeCoverage(base, static_cast<usize>(width) * height, cutoff, 1.0f);
		if (refCoverage <= 0.0) return;

		usize offset    = static_cast<usize>(width) * height * 4;
		u32   mipWidth  = width > 1 ? width / 2 : 1;
		u32   mipHeight = height > 1 ? height / 2 : 1;

		for (u32 mip = 1; mip < mipLevels; ++mip)
		{
			u8*   pixels = base + offset;
			usize count  = static_cast<usize>(mipWidth) * mipHeight;

			f32 lo    = 0.0f;
			f32 hi    = 4.0f;
			f32 scale = 1.0f;
			for (u32 iter = 0; iter < 12; ++iter)
			{
				scale = 0.5f * (lo + hi);
				if (computeCoverage(pixels, count, cutoff, scale) < refCoverage)
				{
					lo = scale;
				}
				else
				{
					hi = scale;
				}
			}

			for (usize i = 0; i < count; ++i)
			{
				f32 a = (static_cast<f32>(pixels[i * 4 + 3]) / 255.0f) * scale;
				a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
				pixels[i * 4 + 3] = static_cast<u8>(a * 255.0f + 0.5f);
			}

			offset += count * 4;
			mipWidth  = mipWidth > 1 ? mipWidth / 2 : 1;
			mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
		}
	}

	void ProcessTextureAsset(RID texture, StringView name, const TextureImportSettings& settings, TextureFormat format, u32 width, u32 height, VoidPtr bytes, UndoRedoScope* scope)
	{

		u32 formatSize = GetTextureFormatSize(format);
		u32 originalUncompressedSize = width * height * formatSize;

		bool generateMips = format != TextureFormat::R32G32B32A32_FLOAT; //mips - not going to generate for hdri.
		u32 mipLevels = generateMips ? static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;

		CompressionMode compressionMode = CompressionMode::None;

		//compress only R32G32B32A32_FLOAT for now.
		if (format == TextureFormat::R32G32B32A32_FLOAT)
		{
			compressionMode = CompressionMode::ZSTD;
		}

		Span<u8> toCompressSpan;
		ByteBuffer compressedBuffer = {};
		ByteBuffer mipsBuffer = {};
		usize totalUncompressedSize{};
		usize totalCompressedSize{};

		if (generateMips && mipLevels > 1) {

			SinglePassDownsampler singlePassDownsampler;

			GPUTexture* gpuTexture = Graphics::CreateTexture(TextureDesc{
				.extent = {width, height, 1},
				.mipLevels = mipLevels,
				.format = format,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest | ResourceUsage::UnorderedAccess | ResourceUsage::CopySource,
				.debugName = String(name) + "_Texture_MipGen"
			});

			{
				u32 mipWidth = width;
				u32 mipHeight = height;
				for (u32 i = 0; i < mipLevels; i++)
				{
					totalUncompressedSize += mipWidth * mipHeight * formatSize;
					if (mipWidth > 1) mipWidth /= 2;
					if (mipHeight > 1) mipHeight /= 2;
				}
			}

			GPUBuffer* srcBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = originalUncompressedSize,
				.usage = ResourceUsage::CopySource,
				.hostVisible = true,
				.persistentMapped = true
			});

			GPUBuffer* dstBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = totalUncompressedSize,
				.usage = ResourceUsage::CopyDest,
				.hostVisible = true,
				.persistentMapped = true
			});

			memcpy(srcBuffer->Map(), bytes, originalUncompressedSize);
			singlePassDownsampler.Init(gpuTexture);

			GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
			cmd->Begin();
			cmd->ResourceBarrier(gpuTexture, ResourceState::Undefined, ResourceState::ShaderReadOnly, 1, mipLevels - 1, 0, 1);

			cmd->ResourceBarrier(gpuTexture, ResourceState::Undefined, ResourceState::CopyDest, 0, 0);
			cmd->CopyBufferToTexture({
				.buffer = srcBuffer,
				.texture = gpuTexture,
				.extent = {width, height, 1},
			});
			cmd->ResourceBarrier(gpuTexture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);

			singlePassDownsampler.Execute(cmd);

			cmd->ResourceBarrier(gpuTexture, ResourceState::ShaderReadOnly, ResourceState::CopySource, 0, mipLevels, 0, 1);

			{
				u32 offset{};
				u32 mipWidth = width;
				u32 mipHeight = height;
				for (u32 m = 0; m < mipLevels; m++)
				{
					cmd->CopyTextureToBuffer({
						.buffer = dstBuffer,
						.texture = gpuTexture,
						.extent = {mipWidth, mipHeight, 1},
						.mipLevel = m,
						.bufferOffset = offset,
					});
					offset += mipWidth * mipHeight * formatSize;

					if (mipWidth > 1) mipWidth /= 2;
					if (mipHeight > 1) mipHeight /= 2;
				}
			}

			cmd->End();

			Graphics::SubmitGPUWork(cmd, true);
			Graphics::AddFreeCommandBuffer(cmd);

			mipsBuffer.Resize(totalUncompressedSize);
			memcpy(mipsBuffer.begin(), dstBuffer->Map(), totalUncompressedSize);

			gpuTexture->Destroy();
			srcBuffer->Destroy();
			dstBuffer->Destroy();

			if (settings.preserveAlphaCoverage && format == TextureFormat::R8G8B8A8_UNORM)
			{
				ScaleAlphaToCoverage(mipsBuffer, width, height, mipLevels, settings.alphaCoverageCutoff);
			}

			toCompressSpan = {mipsBuffer.begin(), totalUncompressedSize};
		}
		else
		{
			totalUncompressedSize = originalUncompressedSize;
			toCompressSpan = {static_cast<u8*>(bytes), totalUncompressedSize};
		}

		Array<RID> imageResources;

		u32 mipWidth = width;
		u32 mipHeight = height;

		if (compressionMode != CompressionMode::None)
		{
			compressedBuffer.Resize(totalUncompressedSize);
		}

		u32 originOffset{};
		u32 compressedOffset{};

		for (u32 mip = 0; mip < mipLevels; mip++)
		{
			usize totalMipSize = mipWidth * mipHeight * formatSize;
			u64 compressedSize = totalMipSize;

			if (compressionMode != CompressionMode::None)
			{
				compressedSize = Compression::Compress(compressedBuffer.begin() + totalCompressedSize,
				                                       compressedBuffer.Size() - totalCompressedSize,
				                                       toCompressSpan.begin() + originOffset,
				                                       totalMipSize,
				                                       compressionMode);

				totalCompressedSize += compressedSize;
			}

			RID imageResource = Resources::Create<TextureImageResource>(UUID::RandomUUID(), scope);
			ResourceObject textureImageObject = Resources::Write(imageResource);
			textureImageObject.SetVec2(TextureImageResource::Extent, Vec2(static_cast<f32>(mipWidth), static_cast<f32>(mipHeight)));
			textureImageObject.SetUInt(TextureImageResource::Mip, mip);
			textureImageObject.SetUInt(TextureImageResource::ArrayLayer, 0);

			//uncompressed data?
			textureImageObject.SetUInt(TextureImageResource::DataOffset, compressedOffset);
			textureImageObject.SetUInt(TextureImageResource::DataSize, compressedSize);
			textureImageObject.SetUInt(TextureImageResource::UncompressedSize, totalMipSize);
			textureImageObject.Commit(scope);

			originOffset += totalMipSize;
			compressedOffset += compressedSize;

			imageResources.EmplaceBack(imageResource);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		Span<u8> byteSpan;
		if (compressionMode != CompressionMode::None)
		{
			byteSpan = {compressedBuffer.begin(), totalCompressedSize};
		}
		else
		{
			byteSpan = {mipsBuffer.begin(), totalUncompressedSize};
		}

		ResourceObject textureObject = Resources::Write(texture);
		textureObject.SetString(TextureResource::Name, name);
		textureObject.SetEnum(TextureResource::Format, format);
		textureObject.SetUInt(TextureResource::MipLevels, mipLevels);
		textureObject.SetEnum(TextureResource::WrapMode, settings.wrapMode);
		textureObject.SetEnum(TextureResource::FilterMode, settings.filterMode);
		textureObject.SetEnum(TextureResource::CompressionMode, compressionMode);
		textureObject.SetUInt(TextureResource::TotalUncompressedSize, totalUncompressedSize);
		textureObject.AddToSubObjectList(TextureResource::Images, imageResources);
		textureObject.SetBuffer(TextureResource::PixelData, ResourceAssets::CreateTempBuffer(byteSpan.begin(), byteSpan.Size()));
		textureObject.Commit(scope);
	}

	void ProcessFromFileTexture(RID texture, TextureImportSettings settings, String path, UndoRedoScope* scope)
	{
		i32 width{};
		i32 height{};
		i32 channels{};
		i32 desiredChannels = 4;
		TextureFormat format;

		bool hdr = Path::Extension(path) == ".hdr";

		VoidPtr bytes = nullptr;

		if (!hdr)
		{
			bytes = stbi_load(path.CStr(), &width, &height, &channels, desiredChannels);
			format = TextureFormat::R8G8B8A8_UNORM;
		}
		else
		{
			bytes = stbi_loadf(path.CStr(), &width, &height, &channels, desiredChannels);
			format = TextureFormat::R32G32B32A32_FLOAT;
		}

		if (bytes == nullptr)
		{
			logger.Error("Failed to load texture: {}", path);
			return;
		}

		ProcessTextureAsset(texture, Path::Name(path), settings, format, width, height, bytes, scope);

		stbi_image_free(bytes);
	}

	RID ImportTexture(RID directory, const TextureImportSettings& settings, const String& path, UndoRedoScope* scope)
	{
		if (!settings.overrideIfExists)
		{
			if (RID texture = ResourceAssets::FindAssetOnDirectory(directory, TypeInfo<TextureResource>::ID(), Path::Name(path)))
			{
				logger.Debug("Texture {} already exists", Path::Name(path));
				return texture;
			}
		}


		RID texture = settings.isSubAsset
			? Resources::Create<TextureResource>(UUID::RandomUUID(), scope)
			: ResourceAssets::CreateImportedAsset(directory, TypeInfo<TextureResource>::ID(), Path::Name(path), scope, path);

		if (settings.async)
		{
			struct ImportData
			{
				RID texture;
				TextureImportSettings settings;
				String path;
				UndoRedoScope* scope;
			};

			ImportData* importData = Alloc<ImportData>();
			importData->texture = texture;
			importData->settings = settings;
			importData->path = path;
			importData->scope = scope;

			Editor::AddTask([importData]
			                {
				                ProcessFromFileTexture(importData->texture, importData->settings, importData->path, importData->scope);
				                DestroyAndFree(importData);
			                },
			                String("Importing texture: ") + Path::Name(importData->path));
		}
		else
		{
			ProcessFromFileTexture(texture, settings, path, scope);
		}


		return texture;
	}

	void ProcessFromMemoryTexture(RID texture, TextureImportSettings settings, StringView name, Span<u8> data, UndoRedoScope* scope)
	{
		i32 width{};
		i32 height{};
		i32 channels{};
		i32 desiredChannels = 4;
		u8* bytes = stbi_load_from_memory(data.begin(), data.Size(), &width, &height, &channels, desiredChannels);

		if (bytes == nullptr)
		{
			logger.Error("Failed to load texture from memory: {}", name);
		}

		ProcessTextureAsset(texture, name, settings, TextureFormat::R8G8B8A8_UNORM, width, height, bytes, scope);

		stbi_image_free(bytes);
	}

	RID ImportTextureFromMemory(RID directory, const TextureImportSettings& settings, StringView name, Span<u8> data, UndoRedoScope* scope)
	{
		RID texture = settings.isSubAsset
			? Resources::Create<TextureResource>(UUID::RandomUUID(), scope)
			: ResourceAssets::CreateImportedAsset(directory, TypeInfo<TextureResource>::ID(), name, scope, "");

		if (settings.async)
		{
			struct ImportData
			{
				RID texture;
				TextureImportSettings settings;
				String name;
				ByteBuffer data;
				UndoRedoScope* scope;
			};

			ImportData* importData = Alloc<ImportData>();
			importData->texture = texture;
			importData->settings = settings;
			importData->name = name;
			importData->data = data;
			importData->scope = scope;

			Editor::AddTask([importData]
			                {
				                ProcessFromMemoryTexture(importData->texture, importData->settings, importData->name, importData->data, importData->scope);
				                DestroyAndFree(importData);
			                },
			                String("Importing texture: ") + name);
		}
		else
		{
			ProcessFromMemoryTexture(texture, settings, name, data, scope);
		}


		return texture;
	}
}
