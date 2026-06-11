#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

namespace Skore
{
	struct TexturePreviewGenerator : PreviewGenerator
	{
		SK_CLASS(TexturePreviewGenerator, PreviewGenerator);

		void SetupScene(Scene* scene) override {}

		void GenerateThumbnail() override
		{
			if (ResourceObject assetObject = Resources::Read(asset))
			{
				if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
				{
					if (ResourceObject textureObject = Resources::Read(object))
					{
						StringView    name = textureObject.GetString(TextureResource::Name);
						TextureFormat format = textureObject.GetEnum<TextureFormat>(TextureResource::Format);
						CompressionMode compressionMode = textureObject.GetEnum<CompressionMode>(TextureResource::CompressionMode);
						ResourceBuffer buffer = textureObject.GetBuffer(TextureResource::PixelData);
						Span<RID> images = textureObject.GetSubObjectList(TextureResource::Images);
						if (images.Empty()) return;

						ResourceObject imageObject = Resources::Read(images[0]);
						Vec2 extent = imageObject.GetVec2(TextureImageResource::Extent);
						u32 size = imageObject.GetUInt(TextureImageResource::UncompressedSize);
						u32 compressedSize = imageObject.GetUInt(TextureImageResource::DataSize);

						GPUTexture* src = Graphics::CreateTexture(TextureDesc{
							.extent = {static_cast<u32>(extent.x), static_cast<u32>(extent.y), 1},
							.format = format,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
							.debugName = String(name) + "_Texture"
						});

						GPUBuffer* tempSrcBuffer = Graphics::CreateBuffer(BufferDesc{
							.size = size,
							.usage = ResourceUsage::CopySource,
							.hostVisible = true,
							.persistentMapped = true
						});

						GPUBuffer* tempDstBuffer = Graphics::CreateBuffer(BufferDesc{
							.size = thumbnailSize.width * thumbnailSize.height * 4,
							.usage = ResourceUsage::CopyDest,
							.hostVisible = true,
							.persistentMapped = true
						});

						VoidPtr mem = tempSrcBuffer->GetMappedData();
						if (compressionMode == CompressionMode::None)
						{
							buffer.CopyData(mem, size);
						}
						else
						{
							ByteBuffer byteBuffer = {};
							byteBuffer.Resize(compressedSize);
							buffer.CopyData(byteBuffer.begin(), compressedSize);
							Compression::Decompress(static_cast<u8*>(mem), size, byteBuffer.begin(), byteBuffer.Size(), compressionMode);
						}


						GPUTexture* dst = Graphics::CreateTexture(TextureDesc{
							.extent = {thumbnailSize.width, thumbnailSize.height, 1},
							.format = TextureFormat::R8G8B8A8_UNORM,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess | ResourceUsage::CopySource,
							.debugName = String(name) + "_Texture"
						});

						Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
						{
							cmd->ResourceBarrier(dst, ResourceState::Undefined, ResourceState::ShaderReadOnly, 0, 0);

							cmd->ResourceBarrier(src, ResourceState::Undefined, ResourceState::CopyDest, 0, 0);
							cmd->CopyBufferToTexture({
								.buffer = tempSrcBuffer,
								.texture = src,
								.extent = src->GetDesc().extent,
							});
							cmd->ResourceBarrier(src, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);

							RenderTools::TextureResize(cmd, src, dst);

							cmd->ResourceBarrier(dst, ResourceState::ShaderReadOnly, ResourceState::CopySource, 0, 0);
							cmd->CopyTextureToBuffer({
								.buffer = tempDstBuffer,
								.texture = dst,
								.extent = dst->GetDesc().extent,
							});
							cmd->ResourceBarrier(dst, ResourceState::CopySource, ResourceState::ShaderReadOnly, 0, 0);
						});

						Span data(static_cast<u8*>(tempDstBuffer->GetMappedData()), thumbnailSize.width * thumbnailSize.height * 4);
						ResourceAssets::UpdateThumbnail(asset, data);

						src->Destroy();
						dst->Destroy();
						tempSrcBuffer->Destroy();
						tempDstBuffer->Destroy();
					}
				}
			}
		}
	};

	struct TextureHandler : ResourceAssetHandler
	{
		SK_CLASS(TextureHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".texture";
		}

		void OpenAsset(RID rid) override
		{
			if (ResourceObject assetObject = Resources::Read(rid))
			{

				if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
				{
					Editor::GetActiveWorkspace()->OpenAsset(object);
				}
			}
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<TextureResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Texture";
		}

		TypeID GetPreviewGenerator() override
		{
			return TypeInfo<TexturePreviewGenerator>::ID();
		}
	};

	void RegisterTextureHandler()
	{
		Reflection::Type<TexturePreviewGenerator>();
		Reflection::Type<TextureHandler>();
	}
}
