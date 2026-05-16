#include "Skore/EditorCommon.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct TextureHandler : ResourceAssetHandler
	{
		SK_CLASS(TextureHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".texture";
		}

		void OpenAsset(RID rid) override
		{
			//TODO
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<TextureResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Texture";
		}

		static void GenerateThumbnail(RID asset)
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

						GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
						cmd->Begin();

						cmd->ResourceBarrier(dst, ResourceState::Undefined, ResourceState::ShaderReadOnly, 0, 0);

						cmd->ResourceBarrier(src, ResourceState::Undefined, ResourceState::CopyDest, 0, 0);
						cmd->CopyBufferToTexture(tempSrcBuffer, src, src->GetDesc().extent, 0, 0, 0);
						cmd->ResourceBarrier(src, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);

						RenderTools::TextureResize(cmd, src, dst);

						cmd->ResourceBarrier(dst, ResourceState::ShaderReadOnly, ResourceState::CopySource, 0, 0);
						cmd->CopyTextureToBuffer(dst, tempDstBuffer, 0, dst->GetDesc().extent, 0, 0);
						cmd->ResourceBarrier(dst, ResourceState::CopySource, ResourceState::ShaderReadOnly, 0, 0);

						cmd->End();
						Graphics::SubmitGPUWork(cmd, true);
						Graphics::AddFreeCommandBuffer(cmd);

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

		FnThumbnailGenerator GetThumbnailGenerator(RID rid) const override
		{
			return GenerateThumbnail;
		}
	};

	void RegisterTextureHandler()
	{
		Reflection::Type<TextureHandler>();
	}
}
