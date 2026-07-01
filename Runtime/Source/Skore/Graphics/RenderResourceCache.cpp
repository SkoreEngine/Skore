#include "Skore/Graphics/RenderResourceCache.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <semaphore>
#include <utility>

#include "concurrentqueue.h"
#include "Skore/App.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/Resource/Resources.hpp"

#ifdef _WIN32
		// Windows:
#include <intrin.h>
		inline unsigned long firstbithigh(unsigned long value)
		{
			unsigned long bit_index;
			if (_BitScanReverse(&bit_index, value))
			{
				return 31ul - bit_index;
			}
			return 0;
		}
#else
// Linux:
inline unsigned long firstbithigh(unsigned int value)
		{
			if (value == 0)
			{
				return 0;
			}
			return __builtin_clz(value);
		}
#endif // _WIN32

namespace Skore
{
	const u32 StagingBufferSize = 1024 * 1024 * 35;

	const u32 MeshDataBufferSize       = 500u * 1024u * 1024u;
	const u32 MeshDataMaxAllocations   = 32u * 1024u;
	const u32 MeshDataAllocationAlign  = 16u;

	const u32 MaxTextureMipsToLoad = 6;

	// Number of frames a texture must remain over-detailed (current top mip > requested) before
	// it gets streamed out. Prevents thrashing when the request bounces frame-to-frame.
	const u32 StreamingUnloadDelayFrames = 120;

	namespace
	{
		void ExecuteOnMainThread(std::function<void()> func);
		void WaitForTexture(const TextureResourceCachePtr& tex);
		// Forward-declared so the worker can call it from an inline lambda. Defined after the
		// globals it touches (globalDescriptorSet / globalDescriptorSetAlive).
		void ApplyTextureStreamingSwap(TextureResourceCachePtr textureCache, GPUTexture* newTexture, u32 newSkippedMips);

		Logger& logger = Logger::GetLogger("Skore.RenderResourceCache");

		GPUBuffer*                                  meshDataBuffer = nullptr;
		std::unique_ptr<OffsetAllocator::Allocator> meshDataAllocator;
		std::mutex                                  meshDataAllocatorMutex;

		u32        nextPrimitiveInfoSlot = 0;
		Array<u32> freePrimitiveInfoSlots;
		GPUBuffer* meshPrimitiveInfoBuffer = nullptr;
		u32        meshPrimitiveInfoBufferCapacity = 0;
		u32        meshPrimitiveInfoCount = 0;
		std::mutex primitiveInfoMutex;

		enum class WorkerType : u32
		{
			None,
			Texture,
			StreamTexture,
			Mesh,
			ProceduralMesh,
			GenerateIBL
		};

		//TODO:   - CONCURRENT sharing on resources that cross queues
		//				- Add a VkSemaphore to the worker's transfer submit, and have the next graphics submit wait on it
		//				 (timeline semaphore is cleanest — one semaphore, monotonically increasing values, no per-frame pool).
		class ResourceWorker
		{
		public:
			struct WorkerData
			{
				WorkerType                          type = WorkerType::None;
				ResourceCachePtr                    resourceCache = {};
				std::shared_ptr<std::promise<void>> promise = nullptr;
				// Mesh-only: resolved separately from `promise` once BLAS is installed, so the
				// scene can pick the mesh up as soon as geometry is uploaded without waiting
				// for ray-tracing acceleration structures.
				std::shared_ptr<std::promise<void>> blasPromise = nullptr;

				u32 streamTargetSkippedMips = 0;

				ByteBuffer           proceduralVertexData;
				ByteBuffer           proceduralIndexData;
				Array<MeshPrimitive> proceduralPrimitives;
				u32                  proceduralDstVertexOffset = U32_MAX;
				u32                  proceduralDstIndexOffset  = U32_MAX;
				u32                  proceduralVertexCount     = 0;
				u32                  proceduralStride          = 0;
				bool                 proceduralBuildBlas       = false;
			};

			struct MeshTaskFutures
			{
				std::shared_future<void> uploadComplete;
				std::shared_future<void> blasComplete;
			};

		public:
			ResourceWorker(u32 numWorkerThreads)
			{
				running.store(true, std::memory_order_release);
				for (u32 i = 0; i < numWorkerThreads; i++)
				{
					auto t = std::thread(&ResourceWorker::WorkerLoop, this);
					Platform::SetThreadName(t, String("ResourceWorker ").Append(ToString(i)));
					workerThreads.EmplaceBack(std::move(t));
				}
			}

			~ResourceWorker()
			{
				running.store(false, std::memory_order_release);
				for (usize i = 0; i < workerThreads.Size(); i++)
				{
					workSem.release();
				}

				for (auto& workerThread : workerThreads)
				{
					if (workerThread.joinable())
					{
						workerThread.join();
					}
				}
			}

			std::future<void> AddTask(WorkerType type, ResourceCachePtr resourceCache)
			{
				auto promise = std::make_shared<std::promise<void>>();
				std::future<void> future = promise->get_future();

				AddTask(WorkerData{type, std::move(resourceCache), std::move(promise)});

				return future;
			}

			MeshTaskFutures AddMeshTask(ResourceCachePtr resourceCache)
			{
				auto uploadPromise = std::make_shared<std::promise<void>>();
				auto blasPromise = std::make_shared<std::promise<void>>();

				MeshTaskFutures futures;
				futures.uploadComplete = uploadPromise->get_future().share();
				futures.blasComplete = blasPromise->get_future().share();

				WorkerData data;
				data.type = WorkerType::Mesh;
				data.resourceCache = std::move(resourceCache);
				data.promise = std::move(uploadPromise);
				data.blasPromise = std::move(blasPromise);
				AddTask(std::move(data));

				return futures;
			}

			void AddStreamTextureTask(ResourceCachePtr resourceCache, u32 targetSkippedMips)
			{
				WorkerData data;
				data.type = WorkerType::StreamTexture;
				data.resourceCache = std::move(resourceCache);
				data.streamTargetSkippedMips = targetSkippedMips;
				AddTask(std::move(data));
			}

			MeshTaskFutures AddProceduralMeshTask(WorkerData&& data, bool wantsUploadFuture, bool wantsBlasFuture)
			{
				MeshTaskFutures futures;

				if (wantsUploadFuture)
				{
					auto p = std::make_shared<std::promise<void>>();
					futures.uploadComplete = p->get_future().share();
					data.promise = std::move(p);
				}

				if (wantsBlasFuture)
				{
					auto p = std::make_shared<std::promise<void>>();
					futures.blasComplete = p->get_future().share();
					data.blasPromise = std::move(p);
				}

				data.type = WorkerType::ProceduralMesh;
				AddTask(std::move(data));

				return futures;
			}

			bool Idle()
			{
				return pendingCount.load(std::memory_order_acquire) == 0;
			}

		private:

			void AddTask(WorkerData&& data)
			{
				pendingCount.fetch_add(1, std::memory_order_release);
				load.enqueue(std::move(data));
				workSem.release();
			}

			void WorkerLoop()
			{
				GPUBuffer* stagingBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = StagingBufferSize,
					.usage = ResourceUsage::CopySource,
					.hostVisible = true,
					.persistentMapped = true
				});

				GPUQueue* transferQueue = Graphics::CreateQueue(QueueDesc{.type = QueueType::Transfer});
				GPUCommandBuffer* transferCmd = Graphics::CreateCommandBuffer(QueueType::Transfer);

				GPUQueue* computeQueue = Graphics::CreateQueue(QueueDesc{.type = QueueType::Compute});
				GPUCommandBuffer* computeCmd = Graphics::CreateCommandBuffer(QueueType::Compute);

				GPUBuffer* blasScratchBuffer = nullptr;
				u64        blasScratchSize = 0;

				auto uploadTexturePixels = [&](GPUTexture* targetTexture, u32 skippedMips, ResourceObject& textureObject)
				{
					u32             srcMipLevels = textureObject.GetUInt(TextureResource::MipLevels);
					Format   format = textureObject.GetEnum<Format>(TextureResource::Format);
					CompressionMode compressionMode = textureObject.GetEnum<CompressionMode>(TextureResource::CompressionMode);
					Span<RID>       images = textureObject.GetSubObjectList(TextureResource::Images);
					ResourceBuffer  buffer = textureObject.GetBuffer(TextureResource::PixelData);
					u32             texelSize = GetFormatSize(format);

					u32 mipLevels = std::max(srcMipLevels, 1u) - skippedMips;

					struct PendingCopy
					{
						u32 stagingOffset;
						u32 mip;
						u32 width;
						u32 height;
					};
					Array<PendingCopy> pendingCopies;
					ByteBuffer         scratch;

					u32  stagingFill = 0;
					u32  srcUncompressedOffset = 0;
					u32  srcCompressedOffset = 0;
					bool firstBatch = true;

					auto barrierToCopyDestIfNeeded = [&]()
					{
						if (firstBatch)
						{
							transferCmd->ResourceBarrier(TextureBarrierDesc{.texture = targetTexture, .oldState = ResourceState::Undefined, .newState = ResourceState::CopyDest, .baseMipLevel = 0, .mipLevelCount = mipLevels, .baseArrayLayer = 0, .arrayLayerCount = 1});
							firstBatch = false;
						}
					};

					auto flushBatch = [&]()
					{
						transferCmd->Begin();
						barrierToCopyDestIfNeeded();
						for (const PendingCopy& pc : pendingCopies)
						{
							transferCmd->CopyBufferToTexture({
								.buffer = stagingBuffer,
								.texture = targetTexture,
								.extent = Extent3D{pc.width, pc.height, 1},
								.mipLevel = pc.mip,
								.bufferOffset = pc.stagingOffset,
							});
						}
						transferCmd->End();
						transferQueue->SubmitAndWait(transferCmd);
						transferCmd->Reset();
						pendingCopies.Clear();
						stagingFill = 0;
					};

					auto uploadOversizedMip = [&](u32 width, u32 height, u32 mip, auto&& srcReader)
					{
						u32 bytesPerRow = width * texelSize;
						SK_ASSERT(bytesPerRow > 0 && bytesPerRow <= StagingBufferSize, "single row exceeds staging buffer");

						u32 rowsPerBatch = StagingBufferSize / bytesPerRow;
						u32 yOffset = 0;
						while (yOffset < height)
						{
							u32 rowsThisBatch = std::min(rowsPerBatch, height - yOffset);
							u32 bytesThisBatch = rowsThisBatch * bytesPerRow;

							srcReader(stagingBuffer->GetMappedData(), bytesThisBatch, static_cast<u64>(yOffset) * bytesPerRow);

							transferCmd->Begin();
							barrierToCopyDestIfNeeded();
							transferCmd->CopyBufferToTexture({
								.buffer = stagingBuffer,
								.texture = targetTexture,
								.extent = Extent3D{width, rowsThisBatch, 1},
								.mipLevel = mip,
								.textureOffset = Offset3D{0, static_cast<i32>(yOffset), 0},
							});
							transferCmd->End();
							transferQueue->SubmitAndWait(transferCmd);
							transferCmd->Reset();

							yOffset += rowsThisBatch;
						}
					};

					for (RID imageRID : images)
					{
						ResourceObject imageObject = Resources::Read(imageRID);
						Vec2 extent = imageObject.GetVec2(TextureImageResource::Extent);
						u32  srcMip = imageObject.GetUInt(TextureImageResource::Mip);
						u32  width = static_cast<u32>(extent.width);
						u32  height = static_cast<u32>(extent.height);
						u32  compressedSize = imageObject.GetUInt(TextureImageResource::DataSize);
						u32  uncompressedSize = imageObject.GetUInt(TextureImageResource::UncompressedSize);

						if (srcMip < skippedMips)
						{
							srcUncompressedOffset += uncompressedSize;
							srcCompressedOffset += compressedSize;
							continue;
						}

						u32 mip = srcMip - skippedMips;

						if (uncompressedSize > StagingBufferSize)
						{
							if (!pendingCopies.Empty())
							{
								flushBatch();
							}

							if (compressionMode == CompressionMode::None)
							{
								uploadOversizedMip(width, height, mip,
									[&](VoidPtr dst, u32 size, u64 rowByteOffset)
									{
										buffer.CopyData(dst, size, srcUncompressedOffset + rowByteOffset);
									});
							}
							else
							{
								// Whole-mip decompression to a scratch buffer; then upload from there.
								scratch.Resize(uncompressedSize);
								ByteBuffer compressedBytes;
								compressedBytes.Resize(compressedSize);
								buffer.CopyData(compressedBytes.begin(), compressedSize, srcCompressedOffset);
								Compression::Decompress(scratch.begin(), uncompressedSize, compressedBytes.begin(), compressedSize, compressionMode);

								uploadOversizedMip(width, height, mip,
									[&](VoidPtr dst, u32 size, u64 rowByteOffset)
									{
										memcpy(dst, scratch.begin() + rowByteOffset, size);
									});
							}
						}
						else
						{
							if (stagingFill + uncompressedSize > StagingBufferSize)
							{
								flushBatch();
							}

							u8* dst = static_cast<u8*>(stagingBuffer->GetMappedData()) + stagingFill;

							if (compressionMode == CompressionMode::None)
							{
								buffer.CopyData(dst, uncompressedSize, srcUncompressedOffset);
							}
							else
							{
								scratch.Resize(compressedSize);
								buffer.CopyData(scratch.begin(), compressedSize, srcCompressedOffset);
								Compression::Decompress(dst, uncompressedSize, scratch.begin(), compressedSize, compressionMode);
							}

							pendingCopies.EmplaceBack(PendingCopy{
								stagingFill,
								mip,
								width,
								height
							});

							stagingFill += uncompressedSize;
						}

						srcUncompressedOffset += uncompressedSize;
						srcCompressedOffset += compressedSize;
					}

					if (!pendingCopies.Empty())
					{
						flushBatch();
					}

					transferCmd->Begin();
					transferCmd->ResourceBarrier(TextureBarrierDesc{.texture = targetTexture, .oldState = ResourceState::CopyDest, .newState = ResourceState::ShaderReadOnly, .baseMipLevel = 0, .mipLevelCount = mipLevels, .baseArrayLayer = 0, .arrayLayerCount = 1});
					transferCmd->End();
					transferQueue->SubmitAndWait(transferCmd);
					transferCmd->Reset();
				};

				while (running.load(std::memory_order_acquire))
				{
					workSem.acquire();

					if (!running.load(std::memory_order_acquire))
					{
						break;
					}

					WorkerData data;
					if (!load.try_dequeue(data))
					{
						continue;
					}
					pendingCount.fetch_sub(1, std::memory_order_release);

					switch (data.type)
					{
						case WorkerType::Texture:
						{
							TextureResourceCachePtr textureCache = std::dynamic_pointer_cast<TextureResourceCache>(data.resourceCache);

							if (ResourceObject textureObject = Resources::Read(textureCache->rid))
							{
								uploadTexturePixels(textureCache->texture, textureCache->skippedMips, textureObject);
							}
							break;
						}
						case WorkerType::StreamTexture:
						{
							TextureResourceCachePtr textureCache = std::dynamic_pointer_cast<TextureResourceCache>(data.resourceCache);
							u32 newSkippedMips = data.streamTargetSkippedMips;

							GPUTexture* newTexture = nullptr;
							bool        uploaded   = false;

							if (textureCache)
							{
								if (ResourceObject textureObject = Resources::Read(textureCache->rid))
								{
									StringView    name   = textureObject.GetString(TextureResource::Name);
									Format format = textureObject.GetEnum<Format>(TextureResource::Format);

									u32 mipsToLoad = textureCache->fullMipCount - newSkippedMips;
									u32 newWidth   = std::max(textureCache->fullExtentWidth  >> newSkippedMips, 1u);
									u32 newHeight  = std::max(textureCache->fullExtentHeight >> newSkippedMips, 1u);

									newTexture = Graphics::CreateTexture(TextureDesc{
										.extent    = {newWidth, newHeight, 1},
										.mipLevels = mipsToLoad,
										.format    = format,
										.usage     = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
										.debugName = String(name) + "_Texture_Streaming"
									});

									uploadTexturePixels(newTexture, newSkippedMips, textureObject);
									uploaded = true;
								}
							}

							if (uploaded)
							{
								ExecuteOnMainThread(
									[textureCache, newTexture, newSkippedMips]()
									{
										ApplyTextureStreamingSwap(textureCache, newTexture, newSkippedMips);
									});
							}
							else
							{
								if (newTexture) newTexture->Destroy();
								if (textureCache) textureCache->streamingPending.store(false, std::memory_order_release);
							}
							break;
						}
						case WorkerType::Mesh:
						{
							MeshResourceCachePtr meshCache = std::dynamic_pointer_cast<MeshResourceCache>(data.resourceCache);

							ResourceObject meshObject = Resources::Read(meshCache->rid);
							if (!meshObject) break;

							bool wantsBlas = meshCache->wantsBlas;

							Array<GPUBottomLevelAS*> builtBlas;

							StringView     name = meshObject.GetString(MeshResource::Name);
							ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
							Span<RID>      lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);

							if (lods.Empty())
							{
								logger.Error("Mesh '{}' has no LODs at upload time.", name);
								break;
							}

							u32 lodCount = static_cast<u32>(lods.Size());
							if (lodCount > MaxLods) lodCount = MaxLods;

							ResourceObject lod0 = Resources::Read(lods[0]);
							u64 vertexSrcOffset  = lod0.GetUInt(MeshLodResource::VerticesOffset);
							u32 vertexCount      = static_cast<u32>(lod0.GetUInt(MeshLodResource::VerticesCount));
							u64 vertexBufferSize = static_cast<u64>(vertexCount) * static_cast<u64>(meshCache->stride);

							struct LodIndexRange { u64 srcOffset; u64 sizeBytes; };
							Array<LodIndexRange> idxRanges(lodCount);
							u64 totalIndexBytes = 0;
							for (u32 i = 0; i < lodCount; i++)
							{
								ResourceObject lodObj = Resources::Read(lods[i]);
								idxRanges[i].srcOffset = lodObj.GetUInt(MeshLodResource::IndicesOffset);
								idxRanges[i].sizeBytes = lodObj.GetUInt(MeshLodResource::IndicesCount) * sizeof(u32);
								totalIndexBytes += idxRanges[i].sizeBytes;
							}

							u32 vertexDstOffset = meshCache->vertexByteOffset;
							u32 indexDstOffset  = meshCache->indexByteOffset;
							if (vertexDstOffset == U32_MAX || indexDstOffset == U32_MAX)
							{
								logger.Error("Mesh '{}' has no mesh-data allocation; skipping upload.", name);
								break;
							}

							auto uploadRange = [&](u64 size, u64 srcOffset, u32 dstOffsetBytes)
							{
								u64 done = 0;
								while (done < size)
								{
									u64 chunk = std::min<u64>(StagingBufferSize, size - done);
									buffer.CopyData(stagingBuffer->GetMappedData(), chunk, srcOffset + done);

									transferCmd->Begin();
									transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::CopyDest});
									transferCmd->CopyBuffer(stagingBuffer, meshDataBuffer, chunk, 0, dstOffsetBytes + done);
									transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::CopyDest, .newState = ResourceState::ShaderReadOnly});
									transferCmd->End();
									transferQueue->SubmitAndWait(transferCmd);
									transferCmd->Reset();

									done += chunk;
								}
							};

							if (vertexBufferSize + totalIndexBytes <= StagingBufferSize)
							{
								u8* mapped = static_cast<u8*>(stagingBuffer->GetMappedData());
								buffer.CopyData(mapped, vertexBufferSize, vertexSrcOffset);

								u64 stagingCursor = vertexBufferSize;
								u32 lodDstCursor  = indexDstOffset;
								for (u32 i = 0; i < lodCount; i++)
								{
									buffer.CopyData(mapped + stagingCursor, idxRanges[i].sizeBytes, idxRanges[i].srcOffset);
									stagingCursor += idxRanges[i].sizeBytes;
								}

								transferCmd->Begin();
								transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::CopyDest});
								transferCmd->CopyBuffer(stagingBuffer, meshDataBuffer, vertexBufferSize, 0, vertexDstOffset);

								u64 stagingOffset = vertexBufferSize;
								for (u32 i = 0; i < lodCount; i++)
								{
									transferCmd->CopyBuffer(stagingBuffer, meshDataBuffer, idxRanges[i].sizeBytes, stagingOffset, lodDstCursor);
									stagingOffset += idxRanges[i].sizeBytes;
									lodDstCursor  += static_cast<u32>(idxRanges[i].sizeBytes);
								}
								transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::CopyDest, .newState = ResourceState::ShaderReadOnly});
								transferCmd->End();
								transferQueue->SubmitAndWait(transferCmd);
								transferCmd->Reset();
							}
							else
							{
								uploadRange(vertexBufferSize, vertexSrcOffset, vertexDstOffset);

								u32 lodDstCursor = indexDstOffset;
								for (u32 i = 0; i < lodCount; i++)
								{
									uploadRange(idxRanges[i].sizeBytes, idxRanges[i].srcOffset, lodDstCursor);
									lodDstCursor += static_cast<u32>(idxRanges[i].sizeBytes);
								}
							}

							// Resolve the upload future as soon as the geometry is on the GPU. The scene
							// can render the mesh now; BLAS install is decoupled below and signalled via
							// blasPromise once the structures are visible from the main thread.
							if (data.promise)
							{
								data.promise->set_value();
								data.promise = nullptr;
							}

							if (wantsBlas)
							{
								u32 primitiveCount = static_cast<u32>(meshCache->primitives.Size());
								builtBlas.Resize(primitiveCount, nullptr);

								Array<BottomLevelASDesc> blasDescs(primitiveCount);
								Array<GeometryDesc>      geometries(primitiveCount);
								u64                      maxScratch = 0;

								struct PrimLodSnapshot { u32 firstIndex; u32 indexCount; };
								Array<PrimLodSnapshot> primLod(primitiveCount);
								{
									std::scoped_lock lock(primitiveInfoMutex);
									const GpuMeshPrimitiveInfo* lodInfoBuffer =
										meshPrimitiveInfoBuffer
											? static_cast<const GpuMeshPrimitiveInfo*>(meshPrimitiveInfoBuffer->GetMappedData())
											: nullptr;

									for (u32 p = 0; p < primitiveCount; ++p)
									{
										const MeshPrimitive& prim = meshCache->primitives[p];
										primLod[p].firstIndex = prim.firstIndex + static_cast<u32>(indexDstOffset / sizeof(u32));
										primLod[p].indexCount = prim.indexCount;

										if (lodInfoBuffer && p < meshCache->primitiveInfoSlots.Size())
										{
											u32 slot = meshCache->primitiveInfoSlots[p];
											if (slot != U32_MAX)
											{
												const GpuMeshPrimitiveInfo& info = lodInfoBuffer[slot];
												if (info.lodCount > 0)
												{
													u32 lodIdx = std::min(BlasLod, info.lodCount - 1);
													primLod[p].firstIndex = info.lods[lodIdx].firstIndex;
													primLod[p].indexCount = info.lods[lodIdx].indexCount;
												}
											}
										}
									}
								}

								for (u32 p = 0; p < primitiveCount; ++p)
								{
									u32 firstIndexUnits   = primLod[p].firstIndex;
									u32 indexCountForBlas = primLod[p].indexCount;

									geometries[p] = GeometryDesc{};
									geometries[p].type = GeometryType::Triangles;
									geometries[p].triangles.vertexBuffer = meshDataBuffer;
									geometries[p].triangles.vertexOffset = vertexDstOffset;
									geometries[p].triangles.vertexCount = vertexCount;
									geometries[p].triangles.vertexStride = meshCache->stride;
									geometries[p].triangles.vertexFormat = Format::RGB32_FLOAT;
									geometries[p].triangles.indexBuffer = meshDataBuffer;
									geometries[p].triangles.indexOffset = static_cast<u64>(firstIndexUnits) * sizeof(u32);
									geometries[p].triangles.indexCount = indexCountForBlas;
									geometries[p].triangles.indexType = IndexType::Uint32;
									geometries[p].triangles.opaque = true;

									blasDescs[p] = BottomLevelASDesc{};
									blasDescs[p].geometries = Span<GeometryDesc>(&geometries[p], 1);
									blasDescs[p].flags = BuildAccelerationStructureFlags::PreferFastTrace;
									blasDescs[p].debugName = String(name) + "_BLAS_" + ToString(p);

									builtBlas[p] = Graphics::CreateBottomLevelAS(blasDescs[p]);
									maxScratch = std::max(maxScratch, static_cast<u64>(Graphics::GetAccelerationStructureBuildScratchSize(blasDescs[p])));
								}

								if (maxScratch > blasScratchSize)
								{
									if (blasScratchBuffer) blasScratchBuffer->Destroy();
									blasScratchBuffer = Graphics::CreateBuffer(BufferDesc{
										.size = maxScratch,
										.usage = ResourceUsage::ShaderResource,
										.hostVisible = false,
										.persistentMapped = false,
										.debugName = "MeshWorker_BLASScratch"
									});
									blasScratchSize = maxScratch;
								}

								computeCmd->Begin();
								for (u32 p = 0; p < primitiveCount; ++p)
								{
									computeCmd->BuildBottomLevelAS(builtBlas[p], AccelerationStructureBuildInfo{.scratchBuffer = blasScratchBuffer});
									if (p + 1 < primitiveCount)
									{
										computeCmd->MemoryBarrier();
									}
								}
								computeCmd->End();
								computeQueue->SubmitAndWait(computeCmd);
								computeCmd->Reset();

								auto pendingBlasPromise = std::move(data.blasPromise);
								ExecuteOnMainThread(
									[meshCache, builtBlas = std::move(builtBlas), pendingBlasPromise = std::move(pendingBlasPromise)]() mutable
									{
										if (!builtBlas.Empty())
										{
											meshCache->blasArray = std::move(builtBlas);
										}

										if (pendingBlasPromise) pendingBlasPromise->set_value();
									});
							}
							else if (data.blasPromise)
							{
								// No BLAS to build for this mesh — release the future so anything
								// waiting on it (or the IsBlasReady check) doesn't stall.
								data.blasPromise->set_value();
								data.blasPromise = nullptr;
							}

							break;
						}
						case WorkerType::ProceduralMesh:
						{
							MeshResourceCachePtr meshCache = std::dynamic_pointer_cast<MeshResourceCache>(data.resourceCache);
							if (!meshCache) break;

							const u32 vertexBytes = data.proceduralVertexCount * data.proceduralStride;
							const u32 indexBytes  = static_cast<u32>(data.proceduralIndexData.Size());
							const u32 vertexDstOffset = data.proceduralDstVertexOffset;
							const u32 indexDstOffset  = data.proceduralDstIndexOffset;

							auto uploadFromCpu = [&](u8* src, u64 size, u32 dstOffsetBytes)
							{
								u64 done = 0;
								while (done < size)
								{
									u64 chunk = std::min<u64>(StagingBufferSize, size - done);
									memcpy(stagingBuffer->GetMappedData(), src + done, chunk);

									transferCmd->Begin();
									transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::CopyDest});
									transferCmd->CopyBuffer(stagingBuffer, meshDataBuffer, chunk, 0, dstOffsetBytes + done);
									transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::CopyDest, .newState = ResourceState::ShaderReadOnly});
									transferCmd->End();
									transferQueue->SubmitAndWait(transferCmd);
									transferCmd->Reset();

									done += chunk;
								}
							};

							if (vertexBytes > 0 && vertexDstOffset != U32_MAX)
							{
								if (vertexBytes + indexBytes <= StagingBufferSize)
								{
									u8* mapped = static_cast<u8*>(stagingBuffer->GetMappedData());
									memcpy(mapped, data.proceduralVertexData.begin(), vertexBytes);
									if (indexBytes > 0)
									{
										memcpy(mapped + vertexBytes, data.proceduralIndexData.begin(), indexBytes);
									}

									transferCmd->Begin();
									transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::CopyDest});
									transferCmd->CopyBuffer(stagingBuffer, meshDataBuffer, vertexBytes, 0, vertexDstOffset);
									if (indexBytes > 0 && indexDstOffset != U32_MAX)
									{
										transferCmd->CopyBuffer(stagingBuffer, meshDataBuffer, indexBytes, vertexBytes, indexDstOffset);
									}
									transferCmd->ResourceBarrier(BufferBarrierDesc{.buffer = meshDataBuffer, .oldState = ResourceState::CopyDest, .newState = ResourceState::ShaderReadOnly});
									transferCmd->End();
									transferQueue->SubmitAndWait(transferCmd);
									transferCmd->Reset();
								}
								else
								{
									uploadFromCpu(data.proceduralVertexData.begin(), vertexBytes, vertexDstOffset);
									if (indexBytes > 0 && indexDstOffset != U32_MAX)
									{
										uploadFromCpu(data.proceduralIndexData.begin(), indexBytes, indexDstOffset);
									}
								}
							}
							else if (indexBytes > 0 && indexDstOffset != U32_MAX)
							{
								uploadFromCpu(data.proceduralIndexData.begin(), indexBytes, indexDstOffset);
							}

							if (data.promise)
							{
								data.promise->set_value();
								data.promise = nullptr;
							}

							if (data.proceduralBuildBlas && !data.proceduralPrimitives.Empty())
							{
								const u32 primitiveCount = static_cast<u32>(data.proceduralPrimitives.Size());

								Array<GPUBottomLevelAS*> builtBlas(primitiveCount, nullptr);
								Array<BottomLevelASDesc> blasDescs(primitiveCount);
								Array<GeometryDesc>      geometries(primitiveCount);
								u64                      maxScratch = 0;

								for (u32 p = 0; p < primitiveCount; ++p)
								{
									const MeshPrimitive& prim = data.proceduralPrimitives[p];

									u32 firstIndexUnits = prim.firstIndex + (indexDstOffset / static_cast<u32>(sizeof(u32)));

									geometries[p] = GeometryDesc{};
									geometries[p].type = GeometryType::Triangles;
									geometries[p].triangles.vertexBuffer = meshDataBuffer;
									geometries[p].triangles.vertexOffset = vertexDstOffset;
									geometries[p].triangles.vertexCount  = data.proceduralVertexCount;
									geometries[p].triangles.vertexStride = data.proceduralStride;
									geometries[p].triangles.vertexFormat = Format::RGB32_FLOAT;
									geometries[p].triangles.indexBuffer  = meshDataBuffer;
									geometries[p].triangles.indexOffset  = static_cast<u64>(firstIndexUnits) * sizeof(u32);
									geometries[p].triangles.indexCount   = prim.indexCount;
									geometries[p].triangles.indexType    = IndexType::Uint32;
									geometries[p].triangles.opaque       = true;

									blasDescs[p] = BottomLevelASDesc{};
									blasDescs[p].geometries = Span<GeometryDesc>(&geometries[p], 1);
									blasDescs[p].flags = BuildAccelerationStructureFlags::PreferFastTrace;
									blasDescs[p].debugName = String(meshCache->debugName) + "_BLAS_" + ToString(p);

									builtBlas[p] = Graphics::CreateBottomLevelAS(blasDescs[p]);
									maxScratch = std::max(maxScratch, static_cast<u64>(Graphics::GetAccelerationStructureBuildScratchSize(blasDescs[p])));
								}

								if (maxScratch > blasScratchSize)
								{
									if (blasScratchBuffer) blasScratchBuffer->Destroy();
									blasScratchBuffer = Graphics::CreateBuffer(BufferDesc{
										.size = maxScratch,
										.usage = ResourceUsage::ShaderResource,
										.hostVisible = false,
										.persistentMapped = false,
										.debugName = "MeshWorker_BLASScratch"
									});
									blasScratchSize = maxScratch;
								}

								computeCmd->Begin();
								for (u32 p = 0; p < primitiveCount; ++p)
								{
									computeCmd->BuildBottomLevelAS(builtBlas[p], AccelerationStructureBuildInfo{.scratchBuffer = blasScratchBuffer});
									if (p + 1 < primitiveCount)
									{
										computeCmd->MemoryBarrier();
									}
								}
								computeCmd->End();
								computeQueue->SubmitAndWait(computeCmd);
								computeCmd->Reset();

								auto pendingBlasPromise = std::move(data.blasPromise);
								ExecuteOnMainThread(
									[meshCache, builtBlas = std::move(builtBlas), pendingBlasPromise = std::move(pendingBlasPromise)]() mutable
									{
										Array<GPUBottomLevelAS*> oldBlas = std::move(meshCache->blasArray);
										meshCache->blasArray = std::move(builtBlas);
										for (GPUBottomLevelAS* old : oldBlas)
										{
											if (old) old->Destroy();
										}
										if (pendingBlasPromise) pendingBlasPromise->set_value();
									});
							}
							else if (data.blasPromise)
							{
								data.blasPromise->set_value();
								data.blasPromise = nullptr;
							}

							break;
						}
						case WorkerType::GenerateIBL:
						{
							MaterialResourceCachePtr materialCache = std::dynamic_pointer_cast<MaterialResourceCache>(data.resourceCache);

							if (!materialCache->skyMaterialTexture->IsLoaded())
							{
								AddTask(std::move(data));
								continue;
							}

							computeCmd->Begin();

							EquirectangularToCubeMap equirectangularToCubemap;
							equirectangularToCubemap.Init();
							equirectangularToCubemap.Execute(computeCmd, materialCache->skyMaterialTexture->texture, materialCache->cubeMapSkyTexture);

							SinglePassDownsampler downsampler;
							downsampler.Init(materialCache->cubeMapSkyTexture);
							downsampler.Execute(computeCmd);

							DiffuseIrradianceGenerator diffuseIrradianceGenerator;
							diffuseIrradianceGenerator.Init();
							diffuseIrradianceGenerator.Execute(computeCmd, materialCache->cubeMapSkyTexture, materialCache->diffuseIrradianceTexture);

							SpecularMapGenerator specularMapGenerator;
							specularMapGenerator.Init(materialCache->cubeMapSkyTexture, materialCache->specularMapTexture);
							specularMapGenerator.Execute(computeCmd);

							computeCmd->End();
							computeQueue->SubmitAndWait(computeCmd);
							computeCmd->Reset();

							equirectangularToCubemap.Destroy();
							diffuseIrradianceGenerator.Destroy();
							break;
						}
						default:
							break;
					}

					if (data.promise)
					{
						data.promise->set_value();
					}
					if (data.blasPromise)
					{
						data.blasPromise->set_value();
					}
				}

				stagingBuffer->Destroy();
				transferQueue->Destroy();
				transferCmd->Destroy();
				computeQueue->Destroy();
				computeCmd->Destroy();
				if (blasScratchBuffer) blasScratchBuffer->Destroy();
			}

			std::atomic<bool>  running{false};
			Array<std::thread> workerThreads;

			moodycamel::ConcurrentQueue<WorkerData> load;
			std::atomic<i32>                        pendingCount{0};
			std::counting_semaphore<>               workSem{0};
		};

		HashMap<RID, FontResourceCachePtr>                 fontCache;
		HashMap<RID, std::weak_ptr<TextureResourceCache>>  textureAsyncCache;
		HashMap<RID, std::weak_ptr<TextureResourceCache>>  textureCache;
		HashMap<RID, std::weak_ptr<MaterialResourceCache>> materialCache;
		HashMap<RID, std::weak_ptr<MeshResourceCache>>     meshCache;
		HashMap<RID, std::weak_ptr<SkinResourceCache>>     skinCache;


		std::mutex fontCacheMutex{};
		std::mutex materialCacheMutex{};
		std::mutex textureCacheMutex{};
		std::mutex meshCacheMutex{};
		std::mutex skinCacheMutex{};

		u64 meshReloadVersion = 0;

		u32               nextMaterialIndex = 0;
		u32               nextTextureIndex = 0;
		Array<u32>        freeMaterialIndices;
		Array<u32>        freeTextureIndices;
		GPUDescriptorSet* globalDescriptorSet = nullptr;
		GPUBuffer*        materialDataBuffer = nullptr;
		GPUBuffer*        materialMaskBuffer = nullptr;
		GPUBuffer*        materialMaskReadbackBuffers[SK_FRAMES_IN_FLIGHT] = {};
		u32               materialDataBufferCapacity = 0;
		u32               materialDataCount = 0;
		GPUBuffer*        materialParamBuffer = nullptr;
		u32               materialParamBufferCapacity = 0;
		u32               materialParamCount = 0;
		bool              globalDescriptorSetAlive = false;

		u32 AllocatePrimitiveInfoSlot()
		{
			std::scoped_lock lock(primitiveInfoMutex);
			if (!freePrimitiveInfoSlots.Empty())
			{
				u32 idx = freePrimitiveInfoSlots.Back();
				freePrimitiveInfoSlots.PopBack();
				return idx;
			}
			return nextPrimitiveInfoSlot++;
		}

		// Caller must hold primitiveInfoMutex.
		void EnsurePrimitiveInfoCapacity(u32 requiredCapacity)
		{
			if (requiredCapacity <= meshPrimitiveInfoBufferCapacity) return;

			u32 newCapacity = requiredCapacity * 2;
			if (newCapacity < 1024) newCapacity = 1024;

			GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = newCapacity * sizeof(GpuMeshPrimitiveInfo),
				.usage = ResourceUsage::ShaderResource,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "MeshPrimitiveInfoBuffer"
			});

			if (meshPrimitiveInfoBuffer)
			{
				memcpy(newBuffer->GetMappedData(),
				       meshPrimitiveInfoBuffer->GetMappedData(),
				       meshPrimitiveInfoBufferCapacity * sizeof(GpuMeshPrimitiveInfo));
				meshPrimitiveInfoBuffer->Destroy();
			}

			meshPrimitiveInfoBuffer = newBuffer;
			meshPrimitiveInfoBufferCapacity = newCapacity;

			if (globalDescriptorSetAlive && globalDescriptorSet)
			{
				globalDescriptorSet->UpdateBuffer(6, meshPrimitiveInfoBuffer, 0,
				                                  meshPrimitiveInfoBufferCapacity * sizeof(GpuMeshPrimitiveInfo));
			}
		}

		void WriteMeshPrimitiveInfo(u32 slot, const GpuMeshPrimitiveInfo& info)
		{
			std::scoped_lock lock(primitiveInfoMutex);
			EnsurePrimitiveInfoCapacity(slot + 1);

			GpuMeshPrimitiveInfo* mapped = static_cast<GpuMeshPrimitiveInfo*>(meshPrimitiveInfoBuffer->GetMappedData());
			mapped[slot] = info;

			if (slot + 1 > meshPrimitiveInfoCount)
			{
				meshPrimitiveInfoCount = slot + 1;
			}
		}

		void FreePrimitiveInfoSlot(u32 slot)
		{
			if (slot == U32_MAX) return;
			std::scoped_lock lock(primitiveInfoMutex);
			freePrimitiveInfoSlots.EmplaceBack(slot);
		}

		std::unique_ptr<ResourceWorker> worker;

		std::mutex                        mainThreadTasksMutex;
		std::queue<std::function<void()>> mainThreadTasks;

		void ExecuteOnMainThread(std::function<void()> func)
		{
			std::scoped_lock lock(mainThreadTasksMutex);
			mainThreadTasks.emplace(std::move(func));
		}

		void ApplyTextureStreamingSwap(TextureResourceCachePtr textureCache, GPUTexture* newTexture, u32 newSkippedMips)
		{
			GPUTexture* oldTexture = textureCache->texture;
			textureCache->texture = newTexture;
			textureCache->skippedMips = newSkippedMips;

			if (globalDescriptorSetAlive && globalDescriptorSet && textureCache->textureIndex != U32_MAX)
			{
				DescriptorUpdate texUpdate;
				texUpdate.type = DescriptorType::SampledImage;
				texUpdate.binding = 3;
				texUpdate.arrayElement = textureCache->textureIndex;
				texUpdate.texture = newTexture;
				globalDescriptorSet->Update(texUpdate);
			}

			if (oldTexture) oldTexture->Destroy();
			textureCache->streamingPending.store(false, std::memory_order_release);
		}

		// Round size up so all OffsetAllocator returned offsets stay 16-byte aligned —
		// safe for any vertex attribute (float4 tangent, etc.) and for u32 indices.
		u32 AlignMeshAllocSize(u64 size)
		{
			return static_cast<u32>((size + MeshDataAllocationAlign - 1) & ~static_cast<u64>(MeshDataAllocationAlign - 1));
		}

		OffsetAllocator::Allocation AllocateMeshData(u32 alignedSize)
		{
			std::scoped_lock lock(meshDataAllocatorMutex);
			return meshDataAllocator->allocate(alignedSize);
		}

		void FreeMeshData(OffsetAllocator::Allocation alloc)
		{
			std::scoped_lock lock(meshDataAllocatorMutex);
			meshDataAllocator->free(alloc);
		}

		u32 AllocateMaterialIndex()
		{
			if (!freeMaterialIndices.Empty())
			{
				u32 idx = freeMaterialIndices.Back();
				freeMaterialIndices.PopBack();
				return idx;
			}
			return nextMaterialIndex++;
		}

		u32 AllocateTextureIndex()
		{
			if (!freeTextureIndices.Empty())
			{
				u32 idx = freeTextureIndices.Back();
				freeTextureIndices.PopBack();
				return idx;
			}
			return nextTextureIndex++;
		}

		struct VertexLayoutOffsetData
		{
			u32 stride;
			u32 posOff;
			u32 normalOff;
			u32 uvOff;
			u32 uv1Off;
			u32 colorOff;
			u32 tangentOff;
			u32 boneIndicesOff;
			u32 boneWeightsOff;
			u32 custom0Off;
			u32 custom1Off;
			u32 custom2Off;
		};

		Array<VertexLayoutOffsetData> vertexLayoutDescs;
		Array<GPUBuffer*>             vertexLayoutBuffers;

		u32 FindOrCreateVertexLayout(const VertexLayoutOffsetData& layoutData)
		{
			for (u32 i = 0; i < vertexLayoutDescs.Size(); ++i)
			{
				if (memcmp(&vertexLayoutDescs[i], &layoutData, sizeof(VertexLayoutOffsetData)) == 0)
				{
					return i;
				}
			}

			u32 idx = static_cast<u32>(vertexLayoutDescs.Size());
			vertexLayoutDescs.EmplaceBack(layoutData);

			GPUBuffer* buffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(VertexLayoutOffsetData),
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "VertexLayout_" + ToString(idx)
			});
			memcpy(buffer->GetMappedData(), &layoutData, sizeof(VertexLayoutOffsetData));
			vertexLayoutBuffers.EmplaceBack(buffer);

			DescriptorUpdate layoutUpdate;
			layoutUpdate.type = DescriptorType::UniformBuffer;
			layoutUpdate.binding = 5;
			layoutUpdate.arrayElement = idx;
			layoutUpdate.buffer = buffer;
			globalDescriptorSet->Update(layoutUpdate);

			return idx;
		}

		struct MaterialData
		{
			Vec3 baseColor;
			f32  roughness;

			f32  metallic;
			i32  baseColorTexture;
			i32  normalTexture;
			i32  roughnessTexture;

			i32  metallicTexture;
			Vec2 uvScale;
			f32  emissiveFactor;

			Vec3 emissiveColor;
			i32  emissiveTexture;

			i32  alphaMode;
			f32  alphaCutoff;
			u32  samplerIndices0;
			u32  samplerIndices1;
		};

		struct PendingMaterial
		{
			MaterialResourceCachePtr cache;
			MaterialData             data;
		};

		std::mutex           pendingMaterialsMutex;
		Array<PendingMaterial> pendingMaterials;

		u32 ResolveSamplerIndex(RID textureRID)
		{
			if (!textureRID) return 0;
			ResourceObject textureObject = Resources::Read(textureRID);
			if (!textureObject) return 0;
			AddressMode wrap    = textureObject.GetEnum<AddressMode>(TextureResource::WrapMode);
			FilterMode  filter  = textureObject.GetEnum<FilterMode>(TextureResource::FilterMode);
			bool        clamp   = wrap == AddressMode::ClampToEdge || wrap == AddressMode::ClampToBorder;
			bool        nearest = filter == FilterMode::Nearest;
			if (clamp) return nearest ? 3u : 2u;
			return nearest ? 1u : 0u;
		}

		void PackGraphMaterialParams(const MaterialResourceCachePtr& materialCache, const MaterialParamLayout& layout, bool async)
		{
			materialCache->materialParamData.Clear();
			if (layout.size == 0)
			{
				return;
			}

			materialCache->materialParamData.Resize(layout.size, 0);
			u8* dest = materialCache->materialParamData.Data();

			ResourceObject matObj = Resources::Read(materialCache->rid);
			bool           isInstance = matObj && matObj.GetEnum<MaterialGraphResource::MaterialKind>(MaterialGraphResource::Kind) == MaterialGraphResource::MaterialKind::Instance;

			for (const MaterialParamEntry& entry : layout.entries)
			{
				Vec4 value{};
				RID  texture{};

				if (ResourceObject nodeObj = Resources::Read(entry.node))
				{
					value = nodeObj.GetVec4(MaterialGraphNodeResource::Value);
					texture = nodeObj.GetReference(MaterialGraphNodeResource::Texture);
				}

				if (isInstance && !entry.name.Empty())
				{
					for (RID overrideRid : matObj.GetSubObjectList(MaterialGraphResource::Parameters))
					{
						ResourceObject overrideObj = Resources::Read(overrideRid);
						if (overrideObj && entry.name == overrideObj.GetString(MaterialParameterOverrideResource::ParameterName))
						{
							value = overrideObj.GetVec4(MaterialParameterOverrideResource::Value);
							if (RID overrideTexture = overrideObj.GetReference(MaterialParameterOverrideResource::Texture))
							{
								texture = overrideTexture;
							}
							break;
						}
					}
				}

				u8* at = dest + entry.offset;
				switch (entry.kind)
				{
					case MaterialParamKind::Float:
					{
						f32 v = value.x;
						memcpy(at, &v, sizeof(f32));
						break;
					}
					case MaterialParamKind::Vec2:
					{
						f32 v[2] = {value.x, value.y};
						memcpy(at, v, sizeof(v));
						break;
					}
					case MaterialParamKind::Vec3:
					{
						f32 v[3] = {value.x, value.y, value.z};
						memcpy(at, v, sizeof(v));
						break;
					}
					case MaterialParamKind::Vec4:
					{
						f32 v[4] = {value.x, value.y, value.z, value.w};
						memcpy(at, v, sizeof(v));
						break;
					}
					case MaterialParamKind::Texture:
					{
						i32 index = -1;
						if (texture)
						{
							TextureResourceCachePtr textureCache = RenderResourceCache::GetTextureCache(texture, async);
							if (textureCache && textureCache->textureIndex != U32_MAX)
							{
								index = static_cast<i32>(textureCache->textureIndex);
								materialCache->textures.EmplaceBack(std::move(textureCache));
							}
						}
						u32 sampler = ResolveSamplerIndex(texture);
						memcpy(at, &index, sizeof(i32));
						memcpy(at + sizeof(i32), &sampler, sizeof(u32));
						break;
					}
				}
			}
		}

		void WriteMaterialParamsToBuffer(const MaterialResourceCachePtr& materialCache)
		{
			if (materialCache->materialParamData.Empty() || materialCache->materialIndex == U32_MAX)
			{
				return;
			}

			u32 requiredCapacity = materialCache->materialIndex + 1;
			if (requiredCapacity > materialParamBufferCapacity)
			{
				u32 newCapacity = requiredCapacity * 2;
				if (newCapacity < 64) newCapacity = 64;

				GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = newCapacity * MaterialParamBlockSize,
					.usage = ResourceUsage::ShaderResource,
					.hostVisible = true,
					.persistentMapped = true,
					.debugName = "MaterialParamBuffer"
				});

				if (materialParamBuffer)
				{
					memcpy(newBuffer->GetMappedData(), materialParamBuffer->GetMappedData(), materialParamBufferCapacity * MaterialParamBlockSize);
					materialParamBuffer->Destroy();
				}

				materialParamBuffer = newBuffer;
				materialParamBufferCapacity = newCapacity;
			}

			u8* mapped = static_cast<u8*>(materialParamBuffer->GetMappedData());
			u8* dest = mapped + static_cast<usize>(materialCache->materialIndex) * MaterialParamBlockSize;
			memset(dest, 0, MaterialParamBlockSize);

			usize bytes = materialCache->materialParamData.Size();
			if (bytes > MaterialParamBlockSize) bytes = MaterialParamBlockSize;
			memcpy(dest, materialCache->materialParamData.Data(), bytes);

			materialParamCount = std::max(materialParamCount, materialCache->materialIndex + 1);
			globalDescriptorSet->UpdateBuffer(7, materialParamBuffer, 0, materialParamCount * MaterialParamBlockSize);
		}

		void WriteMaterialDataToBuffer(const MaterialResourceCachePtr& materialCache, const MaterialData& matData)
		{
			if (materialCache->materialIndex == U32_MAX)
			{
				materialCache->materialIndex = AllocateMaterialIndex();
			}

			u32 requiredCapacity = materialCache->materialIndex + 1;
			if (requiredCapacity > materialDataBufferCapacity)
			{
				u32 newCapacity = requiredCapacity * 2;
				if (newCapacity < 64) newCapacity = 64;

				GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = newCapacity * sizeof(MaterialData),
					.usage = ResourceUsage::ShaderResource,
					.hostVisible = true,
					.persistentMapped = true,
					.debugName = "MaterialDataBuffer"
				});

				if (materialDataBuffer)
				{
					memcpy(newBuffer->GetMappedData(), materialDataBuffer->GetMappedData(), materialDataBufferCapacity * sizeof(MaterialData));
					materialDataBuffer->Destroy();
				}

				GPUBuffer* newMaskBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = newCapacity * sizeof(u32),
					.usage = ResourceUsage::UnorderedAccess | ResourceUsage::CopySource | ResourceUsage::CopyDest,
					.hostVisible = false,
					.persistentMapped = false,
					.debugName = "MaterialMaskBuffer"
				});

				for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
				{
					if (materialMaskReadbackBuffers[f])
					{
						materialMaskReadbackBuffers[f]->Destroy();
					}
					materialMaskReadbackBuffers[f] = Graphics::CreateBuffer(BufferDesc{
						.size = newCapacity * sizeof(u32),
						.usage = ResourceUsage::CopyDest,
						.hostVisible = true,
						.persistentMapped = true,
						.debugName = "MaterialMaskBufferReadback"
					});
					memset(materialMaskReadbackBuffers[f]->GetMappedData(), 0, newCapacity * sizeof(u32));
				}

				if (materialMaskBuffer)
				{
					materialMaskBuffer->Destroy();
				}

				materialMaskBuffer = newMaskBuffer;
				materialDataBuffer = newBuffer;
				materialDataBufferCapacity = newCapacity;
			}

			MaterialData* mapped = static_cast<MaterialData*>(materialDataBuffer->GetMappedData());
			mapped[materialCache->materialIndex] = matData;

			materialDataCount = std::max(materialDataCount, materialCache->materialIndex + 1);

			globalDescriptorSet->UpdateBuffer(0, materialDataBuffer, 0, materialDataCount * sizeof(MaterialData));
			globalDescriptorSet->UpdateBuffer(1, materialMaskBuffer, 0, materialDataCount * sizeof(u32));

			if (materialCache->materialGraph)
			{
				WriteMaterialParamsToBuffer(materialCache);
			}
		}

		void AddPendingMaterial(MaterialResourceCachePtr cache, const MaterialData& data)
		{
			std::scoped_lock lock(pendingMaterialsMutex);
			for (PendingMaterial& p : pendingMaterials)
			{
				if (p.cache == cache)
				{
					p.data = data;
					return;
				}
			}
			pendingMaterials.EmplaceBack(PendingMaterial{std::move(cache), data});
		}

		bool UpdateTexture(GPUDescriptorSet* descriptorSet, const TextureResourceCachePtr& cache, u32 slot)
		{
			if (cache && cache->texture)
			{
				descriptorSet->UpdateTexture(slot, cache->texture);
				return true;
			}
			descriptorSet->UpdateTexture(slot, Graphics::GetWhiteTexture());
			return false;
		}

		void WriteMaterialStubData(const MaterialResourceCachePtr& materialCache)
		{
			MaterialData matData{};
			matData.baseColor = Vec3(1.0f, 1.0f, 1.0f);
			matData.baseColorTexture = -1;
			matData.normalTexture = -1;
			matData.roughnessTexture = -1;
			matData.metallicTexture = -1;
			matData.emissiveTexture = -1;
			matData.uvScale = Vec2(1.0f, 1.0f);

			if (materialCache->materialIndex == U32_MAX)
			{
				AddPendingMaterial(materialCache, matData);
			}
			else
			{
				WriteMaterialDataToBuffer(materialCache, matData);
			}
		}

		void UpdateMaterialStorageData(const ResourceObject& materialObject, MaterialResourceCachePtr materialCache, bool async)
		{
			if (Resources::GetType(materialCache->rid) == Resources::FindType<MaterialGraphResource>())
			{
				MaterialParamLayout layout = MaterialParamLayout::Build(materialCache->rid);
				materialCache->materialGraph = layout.owningGraph ? layout.owningGraph : materialCache->rid;

				// Render settings live on the owning graph, so instances inherit them from their parent.
				ResourceObject        owningGraphObject = materialCache->materialGraph != materialCache->rid ? Resources::Read(materialCache->materialGraph) : ResourceObject{};
				const ResourceObject& graphSettings = owningGraphObject ? owningGraphObject : materialObject;

				MaterialGraphResource::GraphAlphaMode alphaMode = graphSettings.GetEnum<MaterialGraphResource::GraphAlphaMode>(MaterialGraphResource::AlphaMode);
				materialCache->transparent = alphaMode == MaterialGraphResource::GraphAlphaMode::Blend;
				materialCache->masked = alphaMode == MaterialGraphResource::GraphAlphaMode::Mask;
				materialCache->type = MaterialResource::MaterialType::Opaque;

				switch (graphSettings.GetEnum<MaterialGraphResource::GraphRenderFace>(MaterialGraphResource::RenderFace))
				{
					case MaterialGraphResource::GraphRenderFace::Front: materialCache->cullMode = CullMode::Back; break;
					case MaterialGraphResource::GraphRenderFace::Back:  materialCache->cullMode = CullMode::Front; break;
					case MaterialGraphResource::GraphRenderFace::Both:  materialCache->cullMode = CullMode::None; break;
				}
				materialCache->depthWrite = graphSettings.HasValue(MaterialGraphResource::DepthWrite) ? graphSettings.GetBool(MaterialGraphResource::DepthWrite) : !materialCache->transparent;
				materialCache->depthTest = graphSettings.HasValue(MaterialGraphResource::DepthTest) ? graphSettings.GetEnum<CompareOp>(MaterialGraphResource::DepthTest) : CompareOp::Greater;

				materialCache->textures.Clear();
				PackGraphMaterialParams(materialCache, layout, async);

				WriteMaterialStubData(materialCache);
				return;
			}

			materialCache->type = materialObject.GetEnum<MaterialResource::MaterialType>(MaterialResource::Type);
			materialCache->textures.Clear();

			if (materialCache->type != MaterialResource::MaterialType::SkyboxEquirectangular)
			{
				WriteMaterialStubData(materialCache);
			}
			else
			{
				RID sphericalTexture = materialObject.GetReference(MaterialResource::SphericalTexture);

				materialCache->descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
					.bindings = {
						DescriptorSetLayoutBinding{
							.binding = 0,
							.descriptorType = DescriptorType::SampledImage
						},
						DescriptorSetLayoutBinding{
							.binding = 1,
							.descriptorType = DescriptorType::Sampler
						}
					}
				});

				materialCache->skyMaterialTexture = sphericalTexture ? RenderResourceCache::GetTextureCache(sphericalTexture, async) : nullptr;

				UpdateTexture(materialCache->descriptorSet, materialCache->skyMaterialTexture, 0);
				materialCache->descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());

				if (materialCache->skyMaterialTexture && materialCache->skyMaterialTexture->texture)
				{
					if (!materialCache->diffuseIrradianceTexture)
					{
						materialCache->diffuseIrradianceTexture = Graphics::CreateTexture(TextureDesc{
							.extent = {64, 64, 1},
							.arrayLayers = 6,
							.format = Format::RGBA16_FLOAT,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
							.cubemap = true,
							.debugName = "SceneRendererViewport_irradianceTexture"
						});
					}

					if (!materialCache->specularMapTexture)
					{
						materialCache->specularMapTexture = Graphics::CreateTexture(TextureDesc{
							.extent = {256, 256, 1},
							.mipLevels = 8,
							.arrayLayers = 6,
							.format = Format::RGBA16_FLOAT,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
							.cubemap = true,
							.debugName = "SceneRendererViewport_SpecularMapTexture"
						});
					}

					if (!materialCache->cubeMapSkyTexture)
					{
						materialCache->cubeMapSkyTexture = Graphics::CreateTexture(TextureDesc{
							.extent = {256, 256, 1},
							.mipLevels = 8,
							.arrayLayers = 6,
							.format = Format::RGBA16_FLOAT,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
							.cubemap = true,
							.debugName = "SceneRendererViewport_CubemapTexture"
						});

					}

					materialCache->iblComplete = worker->AddTask(WorkerType::GenerateIBL, materialCache).share();
				}
			}
		}

		void GraphicsResourceStorageMaterialReload(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			std::unique_lock lock(materialCacheMutex);

			auto it = materialCache.Find(newValue.GetRID());
			if (it != materialCache.end())
			{
				if (auto sp = it->second.lock())
				{
					UpdateMaterialStorageData(newValue, sp, true);
				}
			}
		}

		void ReloadTextureGpuData(const TextureResourceCachePtr& textureStorage, const ResourceObject& textureObject, bool async)
		{
			Span<RID> images = textureObject.GetSubObjectList(TextureResource::Images);
			if (images.Empty()) return;

			StringView    name = textureObject.GetString(TextureResource::Name);
			Format format = textureObject.GetEnum<Format>(TextureResource::Format);

			ResourceObject firstImageObject = Resources::Read(images[0]);
			Vec2           extent = firstImageObject.GetVec2(TextureImageResource::Extent);
			u32            mipLevels = textureObject.GetUInt(TextureResource::MipLevels);
			u32            totalMips = std::max(mipLevels, 1u);
			u32            newExtentW = static_cast<u32>(extent.x);
			u32            newExtentH = static_cast<u32>(extent.y);

			u32 mipsToLoad = async ? ((MaxTextureMipsToLoad == 0 || MaxTextureMipsToLoad >= totalMips) ? totalMips : MaxTextureMipsToLoad) : totalMips;
			textureStorage->skippedMips = totalMips - mipsToLoad;
			textureStorage->fullExtentWidth = newExtentW;
			textureStorage->fullExtentHeight = newExtentH;
			textureStorage->fullMipCount = totalMips;

			u32 baseWidth = std::max(newExtentW >> textureStorage->skippedMips, 1u);
			u32 baseHeight = std::max(newExtentH >> textureStorage->skippedMips, 1u);

			GPUTexture* oldTexture = textureStorage->texture;

			textureStorage->texture = Graphics::CreateTexture(TextureDesc{
				.extent = {baseWidth, baseHeight, 1},
				.mipLevels = mipsToLoad,
				.format = format,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
				.debugName = String(name) + "_Texture"
			});

			Graphics::SetTextureState(textureStorage->texture, ResourceState::Undefined, ResourceState::ShaderReadOnly);
			textureStorage->uploadComplete = worker->AddTask(WorkerType::Texture, textureStorage).share();

			if (textureStorage->textureIndex != U32_MAX && globalDescriptorSetAlive && globalDescriptorSet)
			{

				DescriptorUpdate texUpdate;
				texUpdate.type = DescriptorType::SampledImage;
				texUpdate.binding = 3;
				texUpdate.arrayElement = textureStorage->textureIndex;
				texUpdate.texture = textureStorage->texture;
				globalDescriptorSet->Update(texUpdate);
			}

			if (oldTexture) oldTexture->Destroy();
		}

		void GraphicsResourceStorageTextureReload(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			RID textureRID = newValue.GetRID();

			TextureResourceCachePtr asyncStorage;
			TextureResourceCachePtr fullStorage;
			{
				std::scoped_lock lock(textureCacheMutex);
				auto             it = textureAsyncCache.Find(textureRID);
				if (it != textureAsyncCache.end()) asyncStorage = it->second.lock();
				auto             fullIt = textureCache.Find(textureRID);
				if (fullIt != textureCache.end()) fullStorage = fullIt->second.lock();
			}

			if (asyncStorage)
			{
				ReloadTextureGpuData(asyncStorage, newValue, true);
			}
			if (fullStorage)
			{
				ReloadTextureGpuData(fullStorage, newValue, false);
			}

			Array<MaterialResourceCachePtr> affected;
			{
				std::scoped_lock lock(materialCacheMutex);
				for (auto& it : materialCache)
				{
					if (auto m = it.second.lock())
					{
						for (const TextureResourceCachePtr& tc : m->textures)
						{
							if (tc && tc->rid == textureRID)
							{
								affected.EmplaceBack(m);
								break;
							}
						}
					}
				}
			}

			for (const MaterialResourceCachePtr& m : affected)
			{
				if (ResourceObject materialObject = Resources::Read(m->rid))
				{
					UpdateMaterialStorageData(materialObject, m, true);
				}
			}
		}

		template<typename V, typename Map, typename Mutex>
		auto MakeCacheDeleter(Map& map, Mutex& mutex, RID key)
		{
			return [&map, &mutex, key](V* obj)
			{
				std::unique_lock lock(mutex);
				auto it = map.Find(key);
				if (it != map.end() && it->second.expired())
				{
					map.Erase(it);
				}
				DestroyAndFree(obj);
			};
		}

		void WaitForTexture(const TextureResourceCachePtr& tex)
		{
			if (tex && tex->uploadComplete.valid())
			{
				tex->uploadComplete.wait();
			}
		}

		void EvaluateTextureStreaming(const TextureResourceCachePtr& texCache)
		{
			if (!texCache || !texCache->texture) return;
			if (texCache->fullMipCount == 0) return;
			if (texCache->streamingPending.load(std::memory_order_acquire)) return;

			if (texCache->uploadComplete.valid() && texCache->uploadComplete.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				return;
			}

			u32 minDim = std::min(texCache->fullExtentWidth, texCache->fullExtentHeight);
			if (minDim == 0) return;

			u32 req = (texCache->lastRequestedResolution > 0) ? texCache->lastRequestedResolution : 1u;

			u32 desiredSkippedMips = 0;
			while ((minDim >> (desiredSkippedMips + 1)) >= req
				&& desiredSkippedMips + 1 < texCache->fullMipCount)
			{
				desiredSkippedMips++;
			}

			u32 maxSkipped = (texCache->fullMipCount > MaxTextureMipsToLoad) ? (texCache->fullMipCount - MaxTextureMipsToLoad) : 0u;
			desiredSkippedMips = std::min(desiredSkippedMips, maxSkipped);

			if (desiredSkippedMips < texCache->skippedMips)
			{
				texCache->streamingDecayFrames = 0;

				bool expected = false;
				if (!texCache->streamingPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
				{
					return;
				}
				worker->AddStreamTextureTask(texCache, desiredSkippedMips);
			}
			else if (desiredSkippedMips > texCache->skippedMips)
			{
				texCache->streamingDecayFrames++;
				if (texCache->streamingDecayFrames < StreamingUnloadDelayFrames) return;

				bool expected = false;
				if (!texCache->streamingPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
				{
					return;
				}
				texCache->streamingDecayFrames = 0;
				worker->AddStreamTextureTask(texCache, desiredSkippedMips);
			}
			else
			{
				texCache->streamingDecayFrames = 0;
			}
		}

		void WaitForMaterial(const MaterialResourceCachePtr& mat)
		{
			if (!mat) return;
			if (mat->iblComplete.valid())
			{
				mat->iblComplete.wait();
			}
			for (const auto& tex : mat->textures)
			{
				WaitForTexture(tex);
			}
			WaitForTexture(mat->skyMaterialTexture);
		}

		void WaitForMesh(const MeshResourceCachePtr& mesh)
		{
			if (!mesh) return;
			while (mesh->uploadComplete.valid() && mesh->uploadComplete.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
			{
				RenderResourceCache::Flush();
				std::this_thread::yield();
			}
			for (const auto& mat : mesh->materials)
			{
				WaitForMaterial(mat);
			}
		}

		bool PopulateMeshGpuData(const MeshResourceCachePtr& meshData, const ResourceObject& meshObject, bool async,
		                         VertexLayoutOffsetData& outLayoutData, bool& outLayoutValid)
		{
			StringView name = meshObject.GetString(MeshResource::Name);
			meshData->aabb = AABB{meshObject.GetVec3(MeshResource::AABBMin), meshObject.GetVec3(MeshResource::AABBMax)};
			meshData->lightmapSizeHint = meshObject.GetVec2(MeshResource::LightmapSizeHint);

			// Vertex layout attribute offsets (U32_MAX = not present)
			u32 positionOffset = U32_MAX;
			u32 normalOffset   = U32_MAX;
			u32 uvOffset       = U32_MAX;
			u32 uv1Offset      = U32_MAX;
			u32 colorOffset    = U32_MAX;
			u32 tangentOffset  = U32_MAX;
			u32 boneIndicesOffset = U32_MAX;
			u32 boneWeightsOffset = U32_MAX;
			u32 custom0Offset = U32_MAX;
			u32 custom1Offset = U32_MAX;
			u32 custom2Offset = U32_MAX;

			RID vertexLayoutRID = meshObject.GetSubObject(MeshResource::VertexLayout);
			if (vertexLayoutRID)
			{
				ResourceObject layoutObject = Resources::Read(vertexLayoutRID);
				if (layoutObject)
				{
					meshData->stride = layoutObject.GetUInt(VertexLayoutResource::Stride);

					Span<RID> attrs = layoutObject.GetSubObjectList(VertexLayoutResource::Attributes);
					for (RID attrRID : attrs)
					{
						ResourceObject attrObj = Resources::Read(attrRID);
						if (attrObj)
						{
							StringView attrName = attrObj.GetString(VertexAttributeResource::Name);
							u32 attrOffset = attrObj.GetUInt(VertexAttributeResource::Offset);

							if (attrName == "position") positionOffset = attrOffset;
							else if (attrName == "normal")   normalOffset = attrOffset;
							else if (attrName == "uv0")      uvOffset = attrOffset;
							else if (attrName == "uv1")    { uv1Offset = attrOffset; meshData->hasUV1 = true; }
							else if (attrName == "color")  { colorOffset = attrOffset; meshData->hasColor = true; }
							else if (attrName == "tangent")  tangentOffset = attrOffset;
							else if (attrName == "boneIndices") boneIndicesOffset = attrOffset;
							else if (attrName == "boneWeights") boneWeightsOffset = attrOffset;
							else if (attrName == "custom0") custom0Offset = attrOffset;
							else if (attrName == "custom1") custom1Offset = attrOffset;
							else if (attrName == "custom2") custom2Offset = attrOffset;
						}
					}
				}
			}
			Span<RID> materials = meshObject.GetReferenceArray(MeshResource::Materials);

			ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
			Span<RID>      lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);

			if (lods.Empty())
			{
				logger.Error("Mesh '{}' has no LODs.", name);
				return false;
			}

			u32 lodCount = static_cast<u32>(lods.Size());
			if (lodCount > MaxLods) lodCount = MaxLods;

			struct LodMeta
			{
				u64 indexSrcOffset;
				u64 indexCount;
				u64 primSrcOffset;
				u64 primCount;
				f32 screenSize;
			};
			Array<LodMeta> lodMeta(lodCount);

			u64 totalIndexBytes = 0;
			for (u32 i = 0; i < lodCount; i++)
			{
				ResourceObject lodObj = Resources::Read(lods[i]);
				lodMeta[i].indexSrcOffset = lodObj.GetUInt(MeshLodResource::IndicesOffset);
				lodMeta[i].indexCount     = lodObj.GetUInt(MeshLodResource::IndicesCount);
				lodMeta[i].primSrcOffset  = lodObj.GetUInt(MeshLodResource::PrimitiveOffset);
				lodMeta[i].primCount      = lodObj.GetUInt(MeshLodResource::PrimitiveCount);
				lodMeta[i].screenSize     = static_cast<f32>(lodObj.GetFloat(MeshLodResource::ScreenSize));
				totalIndexBytes += lodMeta[i].indexCount * sizeof(u32);
			}

			ResourceObject lod0 = Resources::Read(lods[0]);
			u64 vertexCountTotal = lod0.GetUInt(MeshLodResource::VerticesCount);
			u64 vertexSrcOffset  = lod0.GetUInt(MeshLodResource::VerticesOffset);
			u64 vertexBytes      = vertexCountTotal * meshData->stride;

			u64 primCountLod0 = lodMeta[0].primCount;

			bool rtSupported = Graphics::GetDevice()->GetFeatures().rayTracing;

			meshData->hasSkin = static_cast<bool>(meshObject.GetSubObject(MeshResource::Skin));
			meshData->wantsBlas = rtSupported && !meshData->hasSkin;

			meshData->primitives.Resize(primCountLod0);
			buffer.CopyData(meshData->primitives.Data(), primCountLod0 * sizeof(MeshPrimitive), lodMeta[0].primSrcOffset);

			u32 alignedVertSize  = AlignMeshAllocSize(vertexBytes);
			u32 alignedIdxSize   = AlignMeshAllocSize(totalIndexBytes);

			meshData->vertexAlloc = AllocateMeshData(alignedVertSize);
			meshData->indexAlloc  = AllocateMeshData(alignedIdxSize);

			if (meshData->vertexAlloc.offset == OffsetAllocator::Allocation::NO_SPACE ||
				meshData->indexAlloc.offset == OffsetAllocator::Allocation::NO_SPACE)
			{
				logger.Error("Mesh '{}' could not fit into shared mesh data buffer ({} MB used).", name, MeshDataBufferSize / (1024 * 1024));
				if (meshData->vertexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
				{
					FreeMeshData(meshData->vertexAlloc);
					meshData->vertexAlloc = OffsetAllocator::Allocation{};
				}
				if (meshData->indexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
				{
					FreeMeshData(meshData->indexAlloc);
					meshData->indexAlloc = OffsetAllocator::Allocation{};
				}
				return false;
			}

			meshData->vertexByteOffset = meshData->vertexAlloc.offset;
			meshData->indexByteOffset  = meshData->indexAlloc.offset;

			meshData->primitiveInfoSlots.Resize(primCountLod0);

			Array<Array<MeshPrimitive>> lodPrims(lodCount);
			for (u32 i = 0; i < lodCount; i++)
			{
				lodPrims[i].Resize(lodMeta[i].primCount);
				buffer.CopyData(lodPrims[i].Data(), lodMeta[i].primCount * sizeof(MeshPrimitive), lodMeta[i].primSrcOffset);
			}

			Array<u32> lodIndexUnitOffset(lodCount);
			{
				u64 cumulativeBytes = 0;
				const u32 indexBaseUnits = meshData->indexByteOffset / static_cast<u32>(sizeof(u32));
				for (u32 i = 0; i < lodCount; i++)
				{
					lodIndexUnitOffset[i] = indexBaseUnits + static_cast<u32>(cumulativeBytes / sizeof(u32));
					cumulativeBytes += lodMeta[i].indexCount * sizeof(u32);
				}
			}

			for (u32 p = 0; p < primCountLod0; p++)
			{
				u32 slot = AllocatePrimitiveInfoSlot();
				meshData->primitiveInfoSlots[p] = slot;

				GpuMeshPrimitiveInfo info{};
				info.lodCount = lodCount;
				for (u32 i = 0; i < lodCount; i++)
				{
					const MeshPrimitive& srcPrim = (p < lodPrims[i].Size())
						? lodPrims[i][p]
						: lodPrims[0][p];

					info.lods[i].firstIndex = lodIndexUnitOffset[i] + srcPrim.firstIndex;
					info.lods[i].indexCount = srcPrim.indexCount;
					info.lods[i].screenSize = lodMeta[i].screenSize;
				}

				WriteMeshPrimitiveInfo(slot, info);
			}

			outLayoutData.stride         = meshData->stride;
			outLayoutData.posOff         = positionOffset;
			outLayoutData.normalOff      = normalOffset;
			outLayoutData.uvOff          = uvOffset;
			outLayoutData.uv1Off         = uv1Offset;
			outLayoutData.colorOff       = colorOffset;
			outLayoutData.tangentOff     = tangentOffset;
			outLayoutData.boneIndicesOff = boneIndicesOffset;
			outLayoutData.boneWeightsOff = boneWeightsOffset;
			outLayoutData.custom0Off     = custom0Offset;
			outLayoutData.custom1Off     = custom1Offset;
			outLayoutData.custom2Off     = custom2Offset;
			outLayoutValid = true;

			auto meshFutures = worker->AddMeshTask(meshData);
			meshData->uploadComplete = std::move(meshFutures.uploadComplete);
			meshData->blasComplete = std::move(meshFutures.blasComplete);

			if (!materials.Empty())
			{
				meshData->materials.Reserve(materials.Size());
				for (usize i = 0; i < materials.Size(); i++)
				{
					MaterialResourceCachePtr material = RenderResourceCache::GetMaterialCache(materials[i], async);
					meshData->materials.EmplaceBack(std::move(material));
				}
			}
			else
			{
				meshData->materials.EmplaceBack(RenderResourceCache::GetMaterialCache(Resources::FindByPath("Skore://MaterialGraphs/DefaultMaterial.matgraph"), async));
			}

			return true;
		}

		void ResetMeshGpuState(const MeshResourceCachePtr& meshData)
		{
			for (GPUBottomLevelAS* blas : meshData->blasArray)
			{
				if (blas) blas->Destroy();
			}
			meshData->blasArray.Clear();

			if (meshDataAllocator)
			{
				if (meshData->vertexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
				{
					FreeMeshData(meshData->vertexAlloc);
				}
				if (meshData->indexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
				{
					FreeMeshData(meshData->indexAlloc);
				}
			}
			meshData->vertexAlloc = OffsetAllocator::Allocation{};
			meshData->indexAlloc = OffsetAllocator::Allocation{};
			meshData->vertexByteOffset = U32_MAX;
			meshData->indexByteOffset = U32_MAX;

			for (u32 slot : meshData->primitiveInfoSlots)
			{
				FreePrimitiveInfoSlot(slot);
			}
			meshData->primitiveInfoSlots.Clear();

			meshData->primitives.Clear();
			meshData->materials.Clear();
			meshData->stride = 0;
			meshData->hasUV1 = false;
			meshData->hasColor = false;
			meshData->hasSkin = false;
		}

		void GraphicsResourceStorageMeshReload(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			logger.Debug("mesh {} reload", newValue.GetString(MeshResource::Name));

			RID meshRID = newValue.GetRID();

			MeshResourceCachePtr meshData;
			{
				std::scoped_lock lock(meshCacheMutex);
				auto it = meshCache.Find(meshRID);
				if (it != meshCache.end())
				{
					meshData = it->second.lock();
				}
			}
			if (!meshData) return;

			auto drain = [](const std::shared_future<void>& fut)
			{
				while (fut.valid() && fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
				{
					RenderResourceCache::Flush();
					std::this_thread::yield();
				}
			};
			drain(meshData->uploadComplete);
			drain(meshData->blasComplete);

			ResetMeshGpuState(meshData);

			VertexLayoutOffsetData layoutData{};
			bool                   layoutValid = false;
			if (PopulateMeshGpuData(meshData, newValue, true, layoutData, layoutValid) && layoutValid)
			{
				std::scoped_lock lock(meshCacheMutex);
				meshData->vertexLayoutId = FindOrCreateVertexLayout(layoutData);
			}

			meshData->version++;
			meshReloadVersion++;
		}
	}

	bool FontResourceCache::GetAdvance(f64& advance, u32 codepoint1, u32 codepoint2)
	{
		auto glyph1 = glyphs.Find(codepoint1);
		auto glyph2 = glyphs.Find(codepoint2);
		if (glyph1 == glyphs.end() || glyph2 == glyphs.end())
		{
			return false;
		}
		advance = glyph1->second.advance;

		auto it = kerning.Find(Pair(glyph1->second.index, glyph2->second.index));
		if (it != kerning.end())
		{
			advance += it->second;
		}
		return true;
	}

	FontResourceCache::~FontResourceCache()
	{
		if (texture) texture->Destroy();
	}

	TextureResourceCache::~TextureResourceCache()
	{
		if (eventRegistered)
		{
			if (ResourceStorage* storage = Resources::GetStorage(rid))
			{
				storage->UnregisterEvent(ResourceEventType::Changed, GraphicsResourceStorageTextureReload, nullptr);
			}
			eventRegistered = false;
		}

		if (textureIndex != U32_MAX)
		{
			if (globalDescriptorSetAlive && globalDescriptorSet)
			{
				DescriptorUpdate texUpdate;
				texUpdate.type = DescriptorType::SampledImage;
				texUpdate.binding = 3;
				texUpdate.arrayElement = textureIndex;
				texUpdate.texture = Graphics::GetWhiteTexture();
				globalDescriptorSet->Update(texUpdate);
			}
			freeTextureIndices.EmplaceBack(textureIndex);
			textureIndex = U32_MAX;
		}
		if (texture) texture->Destroy();
	}

	MaterialResourceCache::~MaterialResourceCache()
	{
		if (eventRegistered)
		{
			if (ResourceStorage* storage = Resources::GetStorage(rid))
			{
				storage->UnregisterEvent(ResourceEventType::Changed, GraphicsResourceStorageMaterialReload, nullptr);
				storage->UnregisterEvent(ResourceEventType::VersionUpdated, GraphicsResourceStorageMaterialReload, nullptr);
			}
			eventRegistered = false;
		}

		if (materialIndex != U32_MAX)
		{
			freeMaterialIndices.EmplaceBack(materialIndex);
			materialIndex = U32_MAX;
		}

		if (descriptorSet) descriptorSet->Destroy();
		if (diffuseIrradianceTexture) diffuseIrradianceTexture->Destroy();
		if (specularMapTexture) specularMapTexture->Destroy();
		if (cubeMapSkyTexture) cubeMapSkyTexture->Destroy();
	}

	MeshResourceCache::~MeshResourceCache()
	{
		if (eventRegistered)
		{
			if (ResourceStorage* storage = Resources::GetStorage(rid))
			{
				storage->UnregisterEvent(ResourceEventType::Changed, GraphicsResourceStorageMeshReload, nullptr);
			}
			eventRegistered = false;
		}

		for (GPUBottomLevelAS* blas : blasArray)
		{
			if (blas) blas->Destroy();
		}
		blasArray.Clear();

		if (meshDataAllocator)
		{
			if (vertexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
			{
				FreeMeshData(vertexAlloc);
			}
			if (indexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
			{
				FreeMeshData(indexAlloc);
			}
		}
		vertexAlloc = OffsetAllocator::Allocation{};
		indexAlloc = OffsetAllocator::Allocation{};
		vertexByteOffset = U32_MAX;
		indexByteOffset = U32_MAX;

		for (u32 slot : primitiveInfoSlots)
		{
			FreePrimitiveInfoSlot(slot);
		}
		primitiveInfoSlots.Clear();
	}

	bool RenderResourceCache::WorkerIdle()
	{
		return worker->Idle();
	}

	void RenderResourceCache::Flush()
	{
		SK_SCOPED_CPU_ZONE("RenderResourceCache::Flush");
		std::queue<std::function<void()>> drained;
		{
			std::scoped_lock lock(mainThreadTasksMutex);
			drained = std::move(mainThreadTasks);
		}
		while (!drained.empty())
		{
			auto& f = drained.front();
			f();
			drained.pop();
		}

		{
			std::scoped_lock lock(pendingMaterialsMutex);
			for (usize i = 0; i < pendingMaterials.Size(); )
			{
				PendingMaterial& pending = pendingMaterials[i];

				bool allLoaded = true;
				for (const TextureResourceCachePtr& tex : pending.cache->textures)
				{
					if (tex && !tex->IsLoaded())
					{
						allLoaded = false;
						break;
					}
				}

				if (allLoaded)
				{
					WriteMaterialDataToBuffer(pending.cache, pending.data);
					if (i + 1 < pendingMaterials.Size())
					{
						pendingMaterials[i] = std::move(pendingMaterials.Back());
					}
					pendingMaterials.PopBack();
				}
				else
				{
					++i;
				}
			}
		}

		if (materialMaskBuffer && materialDataCount > 0)
		{
			u32 slot = static_cast<u32>(App::Frame() % SK_FRAMES_IN_FLIGHT);
			if (materialMaskReadbackBuffers[slot])
			{
				u32* mapped = static_cast<u32*>(materialMaskReadbackBuffers[slot]->GetMappedData());

				Array<TextureResourceCachePtr>  texSnap;
				Array<MaterialResourceCachePtr> matSnap;
				{
					std::scoped_lock lock(textureCacheMutex);
					texSnap.Reserve(textureAsyncCache.Size());
					for (auto& it : textureAsyncCache)
					{
						if (auto sp = it.second.lock()) texSnap.EmplaceBack(std::move(sp));
					}
				}
				{
					std::scoped_lock lock(materialCacheMutex);
					matSnap.Reserve(materialCache.Size());
					for (auto& it : materialCache)
					{
						if (auto sp = it.second.lock()) matSnap.EmplaceBack(std::move(sp));
					}
				}

				for (const TextureResourceCachePtr& t : texSnap)
				{
					t->lastRequestedResolution = 0;
				}

				for (const MaterialResourceCachePtr& m : matSnap)
				{
					if (m->materialIndex != U32_MAX && m->materialIndex < materialDataCount)
					{
						u32 mask = mapped[m->materialIndex];
						u32 requestUVset0 = mask & 0xFFFF;
						if (requestUVset0 != 0)
						{
							m->reqTextureResolution = 1ul << (31ul - firstbithigh(requestUVset0));
						}
						else
						{
							m->reqTextureResolution = 0;
						}
					}

					for (const TextureResourceCachePtr& tex : m->textures)
					{
						if (tex)
						{
							tex->lastRequestedResolution = std::max(tex->lastRequestedResolution, m->reqTextureResolution);
						}
					}
				}

				for (const TextureResourceCachePtr& t : texSnap)
				{
					EvaluateTextureStreaming(t);
				}
			}
		}
	}

	void RenderResourceCache::Flush(GPUCommandBuffer* cmd)
	{
		if (!materialMaskBuffer || materialDataCount == 0) return;

		u32 slot = static_cast<u32>(App::Frame() % SK_FRAMES_IN_FLIGHT);
		GPUBuffer* readback = materialMaskReadbackBuffers[slot];
		if (!readback) return;

		u32 sizeBytes = materialDataCount * sizeof(u32);

		cmd->ResourceBarrier(BufferBarrierDesc{.buffer = materialMaskBuffer, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::CopySource});
		cmd->CopyBuffer(materialMaskBuffer, readback, sizeBytes, 0, 0);
		cmd->ResourceBarrier(BufferBarrierDesc{.buffer = materialMaskBuffer, .oldState = ResourceState::CopySource, .newState = ResourceState::CopyDest});
		cmd->FillBuffer(materialMaskBuffer, 0, sizeBytes, 0);
		cmd->ResourceBarrier(BufferBarrierDesc{.buffer = materialMaskBuffer, .oldState = ResourceState::CopyDest, .newState = ResourceState::ShaderReadOnly});
	}

	FontResourceCachePtr RenderResourceCache::GetFontCache(RID font)
	{
		if (!font) return nullptr;

		std::unique_lock lock(fontCacheMutex);
		auto             it = fontCache.Find(font);
		if (it != fontCache.end())
		{
			return it->second;
		}

		FontResourceCachePtr fontData = std::make_shared<FontResourceCache>(font);
		fontData->fontId = static_cast<u16>(fontCache.Size() + 1);

		FontResourceData data;
		String           name;
		{
			ResourceObject fontObject = Resources::Read(font);
			if (!fontObject) return nullptr;

			name = fontObject.GetString(FontResource::Name);

			Span<u8>  compressedData = fontObject.GetBlob(FontResource::FontData);
			usize     size = fontObject.GetUInt(FontResource::FontDataUncompressedSize);
			Array<u8> uncompressedData(size);
			size = Compression::Decompress(uncompressedData.Data(), uncompressedData.Size(), compressedData.Data(), compressedData.Size(), CompressionMode::ZSTD);
			uncompressedData.Resize(size);

			BinaryArchiveReader reader(uncompressedData);
			reader.BeginMap("data");
			data.Deserialize(reader);
			reader.EndMap();
		}

		fontData->maxHeightGlyph = data.maxHeightGlyph;
		fontData->metrics = data.metrics;

		for (const FontGlyph& glyph : data.glyphs)
		{
			fontData->glyphs.Insert(glyph.codepoint, glyph);
		}

		for (const FontKerning& kerning : data.kernings)
		{
			fontData->kerning.Insert(Pair(kerning.first, kerning.second), kerning.offset);
		}

		fontData->texture = Graphics::CreateTexture(TextureDesc{
			.extent = Extent3D(data.atlas.extent.width, data.atlas.extent.height, 1),
			.format = Format::RGBA32_FLOAT,
			.debugName = "FontAtlas_" + name
		});

		TextureDataInfo textureDataInfo{
			.texture = fontData->texture,
			.data = data.atlas.pixels.Data(),
			.size = data.atlas.pixels.Size()
		};

		Graphics::UploadTextureData(textureDataInfo);

		fontCache.Insert(font, fontData);
		return fontData;
	}

	TextureResourceCachePtr RenderResourceCache::GetTextureCache(RID texture, bool async)
	{
		if (!texture) return nullptr;

		HashMap<RID, std::weak_ptr<TextureResourceCache>>& cache = async ? textureAsyncCache : textureCache;

		// Phase 1: brief lock — return existing cache if any.
		{
			std::unique_lock lock(textureCacheMutex);
			auto it = cache.Find(texture);
			if (it != cache.end())
			{
				if (auto sp = it->second.lock())
				{
					lock.unlock();
					if (!async && sp->uploadComplete.valid()) sp->uploadComplete.wait();
					return sp;
				}
				cache.Erase(it);
			}
		}

		TextureResourceCachePtr textureStorage(Alloc<TextureResourceCache>(texture), MakeCacheDeleter<TextureResourceCache>(cache, textureCacheMutex, texture));

		if (ResourceStorage* storage = Resources::GetStorage(texture))
		{
			storage->RegisterEvent(ResourceEventType::Changed, GraphicsResourceStorageTextureReload, nullptr);
			textureStorage->eventRegistered = true;
		}

		if (ResourceObject textureObject = Resources::Read(texture))
		{
			StringView name = textureObject.GetString(TextureResource::Name);
			Format format = textureObject.GetEnum<Format>(TextureResource::Format);
			Span<RID> images = textureObject.GetSubObjectList(TextureResource::Images);

			ResourceObject firstImageObject = Resources::Read(images[0]);
			Vec2 extent = firstImageObject.GetVec2(TextureImageResource::Extent);
			u32 mipLevels = textureObject.GetUInt(TextureResource::MipLevels);

			u32 totalMips = std::max(mipLevels, 1u);
			u32 mipsToLoad = async
				? ((MaxTextureMipsToLoad == 0 || MaxTextureMipsToLoad >= totalMips) ? totalMips : MaxTextureMipsToLoad)
				: totalMips;
			textureStorage->skippedMips = totalMips - mipsToLoad;
			textureStorage->fullExtentWidth  = static_cast<u32>(extent.x);
			textureStorage->fullExtentHeight = static_cast<u32>(extent.y);
			textureStorage->fullMipCount     = totalMips;

			u32 baseWidth  = std::max(static_cast<u32>(extent.x) >> textureStorage->skippedMips, 1u);
			u32 baseHeight = std::max(static_cast<u32>(extent.y) >> textureStorage->skippedMips, 1u);

			textureStorage->texture = Graphics::CreateTexture(TextureDesc{
				.extent = {baseWidth, baseHeight, 1},
				.mipLevels = mipsToLoad,
				.format = format,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
				.debugName = String(name) + "_Texture"
			});

			Graphics::SetTextureState(textureStorage->texture, ResourceState::Undefined, ResourceState::ShaderReadOnly);

			textureStorage->uploadComplete = worker->AddTask(WorkerType::Texture, textureStorage).share();
		}

		// Phase 3: brief lock — race re-check, then insert + bindless descriptor update.
		TextureResourceCachePtr result;
		{
			std::unique_lock lock(textureCacheMutex);

			auto it = cache.Find(texture);
			if (it != cache.end())
			{
				if (auto sp = it->second.lock())
				{
					result = sp;
				}
				else
				{
					cache.Erase(it);
				}
			}

			if (!result)
			{
				if (textureStorage->texture)
				{
					textureStorage->textureIndex = AllocateTextureIndex();

					DescriptorUpdate texUpdate;
					texUpdate.type = DescriptorType::SampledImage;
					texUpdate.binding = 3;
					texUpdate.arrayElement = textureStorage->textureIndex;
					texUpdate.texture = textureStorage->texture;
					globalDescriptorSet->Update(texUpdate);
				}

				cache.Insert(texture, std::weak_ptr(textureStorage));
				result = textureStorage;
			}
		}

		if (!async && result && result->uploadComplete.valid())
		{
			result->uploadComplete.wait();
		}
		return result;
	}

	namespace
	{
		FnMaterialVariantResolver gMaterialVariantResolver = nullptr;
	}

	void RenderResourceCache::SetMaterialVariantResolver(FnMaterialVariantResolver resolver)
	{
		gMaterialVariantResolver = resolver;
	}

	RID RenderResourceCache::EnsureMaterialVariant(RID shader, RID material, StringView variantName)
	{
		if (gMaterialVariantResolver)
		{
			return gMaterialVariantResolver(shader, material, variantName);
		}
		return ShaderResource::GetVariant(shader, material, variantName);
	}

	MaterialResourceCachePtr RenderResourceCache::GetMaterialCache(RID material, bool async)
	{
		if (!material) return nullptr;
		ResourceObject materialObject = Resources::Read(material);
		if (!materialObject) return nullptr;

		// Phase 1: brief lock — return existing cache if any.
		{
			std::unique_lock lock(materialCacheMutex);
			auto it = materialCache.Find(material);
			if (it != materialCache.end())
			{
				if (auto sp = it->second.lock())
				{
					lock.unlock();
					if (!async) WaitForMaterial(sp);
					return sp;
				}
				materialCache.Erase(it);
			}
		}

		// Phase 2: heavy work without the lock — includes recursive GetTextureCache calls
		// inside UpdateMaterialStorageData and (for skybox) descriptor-set + IBL setup.
		MaterialResourceCachePtr materialData(Alloc<MaterialResourceCache>(material), MakeCacheDeleter<MaterialResourceCache>(materialCache, materialCacheMutex, material));

		if (ResourceStorage* storage = Resources::GetStorage(material))
		{
			storage->RegisterEvent(ResourceEventType::Changed, GraphicsResourceStorageMaterialReload, nullptr);
			storage->RegisterEvent(ResourceEventType::VersionUpdated, GraphicsResourceStorageMaterialReload, nullptr);
			materialData->eventRegistered = true;
		}


		UpdateMaterialStorageData(materialObject, materialData, async);

		// Phase 3: brief lock — race re-check, then insert.
		MaterialResourceCachePtr result;
		{
			std::unique_lock lock(materialCacheMutex);
			auto it = materialCache.Find(material);
			if (it != materialCache.end())
			{
				if (auto sp = it->second.lock())
				{
					result = sp;
				}
				else
				{
					materialCache.Erase(it);
				}
			}

			if (!result)
			{
				materialCache.Insert(material, std::weak_ptr(materialData));
				result = materialData;
			}
		}

		if (!async)
		{
			WaitForMaterial(result);
		}
		return result;
	}

	MeshResourceCachePtr RenderResourceCache::GetMeshCache(RID mesh, bool async)
	{
		if (!mesh) return nullptr;

		// Phase 1: brief lock — return existing cache if any.
		{
			std::unique_lock lock(meshCacheMutex);
			auto it = meshCache.Find(mesh);
			if (it != meshCache.end())
			{
				if (auto sp = it->second.lock())
				{
					lock.unlock();
					if (!async) WaitForMesh(sp);
					return sp;
				}
				meshCache.Erase(it);
			}
		}

		// Phase 2: heavy work without the lock — Resources::Read, mesh-data buffer alloc,
		// worker enqueue, recursive GetMaterialCache calls. Vertex-layout registration is
		// deferred to Phase 3 because it mutates global vertexLayoutDescs.
		MeshResourceCachePtr meshData(Alloc<MeshResourceCache>(mesh), MakeCacheDeleter<MeshResourceCache>(meshCache, meshCacheMutex, mesh));

		if (ResourceStorage* storage = Resources::GetStorage(mesh))
		{
			storage->RegisterEvent(ResourceEventType::Changed, GraphicsResourceStorageMeshReload, nullptr);
			meshData->eventRegistered = true;
		}

		VertexLayoutOffsetData layoutData{};
		bool                   layoutValid = false;

		if (ResourceObject meshObject = Resources::Read(mesh))
		{
			if (!PopulateMeshGpuData(meshData, meshObject, async, layoutData, layoutValid))
			{
				return nullptr;
			}
		}

		// Phase 3: brief lock — race re-check, register vertex layout, insert.
		MeshResourceCachePtr result;
		{
			std::unique_lock lock(meshCacheMutex);
			auto it = meshCache.Find(mesh);
			if (it != meshCache.end())
			{
				if (auto sp = it->second.lock())
				{
					result = sp;
				}
				else
				{
					meshCache.Erase(it);
				}
			}

			if (!result)
			{
				if (layoutValid)
				{
					meshData->vertexLayoutId = FindOrCreateVertexLayout(layoutData);
				}
				meshCache.Insert(mesh, std::weak_ptr<MeshResourceCache>(meshData));
				result = meshData;
			}
		}

		if (!async) WaitForMesh(result);
		return result;
	}

	VertexLayoutCachePtr RenderResourceCache::CreateCustomVoxelLayout(const VertexLayoutDesc& desc)
	{
		if (desc.stride == 0)
		{
			logger.Error("CreateCustomVoxelLayout: stride must be > 0.");
			return nullptr;
		}

		VertexLayoutOffsetData layoutData{};
		layoutData.stride         = desc.stride;
		layoutData.posOff         = U32_MAX;
		layoutData.normalOff      = U32_MAX;
		layoutData.uvOff          = U32_MAX;
		layoutData.uv1Off         = U32_MAX;
		layoutData.colorOff       = U32_MAX;
		layoutData.tangentOff     = U32_MAX;
		layoutData.boneIndicesOff = U32_MAX;
		layoutData.boneWeightsOff = U32_MAX;
		layoutData.custom0Off     = U32_MAX;
		layoutData.custom1Off     = U32_MAX;
		layoutData.custom2Off     = U32_MAX;

		bool hasUV1 = false, hasColor = false, hasSkin = false;

		for (const VertexLayoutAttribute& attr : desc.attributes)
		{
			if (attr.name == "position")          layoutData.posOff = attr.offset;
			else if (attr.name == "normal")       layoutData.normalOff = attr.offset;
			else if (attr.name == "uv0")          layoutData.uvOff = attr.offset;
			else if (attr.name == "uv1")        { layoutData.uv1Off = attr.offset; hasUV1 = true; }
			else if (attr.name == "color")      { layoutData.colorOff = attr.offset; hasColor = true; }
			else if (attr.name == "tangent")      layoutData.tangentOff = attr.offset;
			else if (attr.name == "boneIndices"){ layoutData.boneIndicesOff = attr.offset; hasSkin = true; }
			else if (attr.name == "boneWeights"){ layoutData.boneWeightsOff = attr.offset; hasSkin = true; }
			else if (attr.name == "custom0")      layoutData.custom0Off = attr.offset;
			else if (attr.name == "custom1")      layoutData.custom1Off = attr.offset;
			else if (attr.name == "custom2")      layoutData.custom2Off = attr.offset;
		}

		auto layout = std::make_shared<VertexLayoutCache>();
		layout->vertexLayoutId = FindOrCreateVertexLayout(layoutData);
		layout->stride         = desc.stride;
		layout->hasUV1         = hasUV1;
		layout->hasColor       = hasColor;
		layout->hasSkin        = hasSkin;
		return layout;
	}

	MeshResourceCachePtr RenderResourceCache::CreateProceduralMesh(const ProceduralMeshDesc& desc)
	{
		if (!desc.layout || desc.layout->vertexLayoutId == U32_MAX)
		{
			logger.Error("CreateProceduralMesh: layout is null or invalid.");
			return nullptr;
		}
		if (desc.vertexCount == 0 || desc.vertexData.Size() < static_cast<usize>(desc.vertexCount) * desc.layout->stride)
		{
			logger.Error("CreateProceduralMesh: vertexData ({} bytes) is smaller than vertexCount * stride ({} bytes).",
				desc.vertexData.Size(), desc.vertexCount * desc.layout->stride);
			return nullptr;
		}
		if (desc.indices.Empty() || desc.primitives.Empty())
		{
			logger.Error("CreateProceduralMesh: indices and primitives must be non-empty.");
			return nullptr;
		}
		if (!meshDataAllocator)
		{
			logger.Error("CreateProceduralMesh: mesh data allocator not initialized (engine not started or already shut down).");
			return nullptr;
		}

		MeshResourceCachePtr meshData(Alloc<MeshResourceCache>(RID{}), [](MeshResourceCache* obj)
		{
			DestroyAndFree(obj);
		});

		meshData->isProcedural    = true;
		meshData->layoutCache     = desc.layout;
		meshData->vertexLayoutId  = desc.layout->vertexLayoutId;
		meshData->stride          = desc.layout->stride;
		meshData->hasUV1          = desc.layout->hasUV1;
		meshData->hasColor        = desc.layout->hasColor;
		meshData->hasSkin         = desc.layout->hasSkin;
		meshData->vertexCount     = desc.vertexCount;
		meshData->aabb            = desc.aabb;
		meshData->lightmapSizeHint = desc.lightmapSizeHint;
		meshData->debugName       = desc.debugName;

		bool rtSupported = Graphics::GetDevice()->GetFeatures().rayTracing;
		meshData->wantsBlas = desc.wantsBlas && rtSupported && !meshData->hasSkin;

		const u32 vertexBytes = desc.vertexCount * meshData->stride;
		const u32 indexBytes  = static_cast<u32>(desc.indices.Size() * sizeof(u32));
		const u32 alignedVertSize = AlignMeshAllocSize(vertexBytes);
		const u32 alignedIdxSize  = AlignMeshAllocSize(indexBytes);

		meshData->vertexAlloc = AllocateMeshData(alignedVertSize);
		meshData->indexAlloc  = AllocateMeshData(alignedIdxSize);

		if (meshData->vertexAlloc.offset == OffsetAllocator::Allocation::NO_SPACE ||
			meshData->indexAlloc.offset == OffsetAllocator::Allocation::NO_SPACE)
		{
			logger.Error("CreateProceduralMesh '{}': out of mesh-data buffer space ({} MB total).",
				desc.debugName, MeshDataBufferSize / (1024 * 1024));
			if (meshData->vertexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
			{
				FreeMeshData(meshData->vertexAlloc);
				meshData->vertexAlloc = OffsetAllocator::Allocation{};
			}
			if (meshData->indexAlloc.offset != OffsetAllocator::Allocation::NO_SPACE)
			{
				FreeMeshData(meshData->indexAlloc);
				meshData->indexAlloc = OffsetAllocator::Allocation{};
			}
			return nullptr;
		}

		meshData->vertexByteOffset = meshData->vertexAlloc.offset;
		meshData->indexByteOffset  = meshData->indexAlloc.offset;
		meshData->vertexAllocSize  = alignedVertSize;
		meshData->indexAllocSize   = alignedIdxSize;

		const u32 primCount = static_cast<u32>(desc.primitives.Size());
		meshData->primitives.Resize(primCount);
		for (u32 p = 0; p < primCount; ++p) meshData->primitives[p] = desc.primitives[p];

		const u32 indexBaseUnits = meshData->indexByteOffset / static_cast<u32>(sizeof(u32));
		meshData->primitiveInfoSlots.Resize(primCount);
		for (u32 p = 0; p < primCount; ++p)
		{
			u32 slot = AllocatePrimitiveInfoSlot();
			meshData->primitiveInfoSlots[p] = slot;

			GpuMeshPrimitiveInfo info{};
			info.lodCount = 1;
			info.lods[0].firstIndex = indexBaseUnits + desc.primitives[p].firstIndex;
			info.lods[0].indexCount = desc.primitives[p].indexCount;
			info.lods[0].screenSize = 0.0f;
			WriteMeshPrimitiveInfo(slot, info);
		}

		if (!desc.materials.Empty())
		{
			meshData->materials.Reserve(desc.materials.Size());
			for (const MaterialResourceCachePtr& m : desc.materials)
			{
				meshData->materials.EmplaceBack(m);
			}
		}
		else
		{
			meshData->materials.EmplaceBack(GetMaterialCache(Resources::FindByPath("Skore://MaterialGraphs/DefaultMaterial.matgraph"), true));
		}

		ResourceWorker::WorkerData wd;
		wd.resourceCache              = meshData;
		wd.proceduralVertexData.Resize(vertexBytes);
		memcpy(wd.proceduralVertexData.begin(), desc.vertexData.begin(), vertexBytes);
		wd.proceduralIndexData.Resize(indexBytes);
		memcpy(wd.proceduralIndexData.begin(), desc.indices.begin(), indexBytes);
		wd.proceduralPrimitives.Resize(primCount);
		for (u32 p = 0; p < primCount; ++p) wd.proceduralPrimitives[p] = desc.primitives[p];
		wd.proceduralDstVertexOffset = meshData->vertexByteOffset;
		wd.proceduralDstIndexOffset  = meshData->indexByteOffset;
		wd.proceduralVertexCount     = desc.vertexCount;
		wd.proceduralStride          = meshData->stride;
		wd.proceduralBuildBlas       = meshData->wantsBlas;

		auto futures = worker->AddProceduralMeshTask(std::move(wd), true, meshData->wantsBlas);
		meshData->uploadComplete = std::move(futures.uploadComplete);
		meshData->blasComplete   = std::move(futures.blasComplete);

		return meshData;
	}

	void RenderResourceCache::UpdateProceduralMesh(const MeshResourceCachePtr& mesh, const ProceduralMeshUpdate& update)
	{
		if (!mesh)
		{
			logger.Error("UpdateProceduralMesh: mesh is null.");
			return;
		}
		if (!mesh->isProcedural)
		{
			logger.Error("UpdateProceduralMesh: mesh is asset-backed (RID = {}), updates only work on procedural meshes.", mesh->rid.id);
			return;
		}
		if (!meshDataAllocator)
		{
			logger.Error("UpdateProceduralMesh: mesh data allocator not initialized.");
			return;
		}

		const bool hasVertexUpdate = !update.vertexData.Empty() && update.vertexCount > 0;
		const bool hasIndexUpdate  = !update.indices.Empty();
		const bool hasPrimUpdate   = !update.primitives.Empty();
		const u32  stride          = mesh->stride;

		if (hasVertexUpdate)
		{
			const u32 vertexBytes = update.vertexCount * stride;
			if (update.vertexData.Size() < static_cast<usize>(vertexBytes))
			{
				logger.Error("UpdateProceduralMesh: vertexData ({} bytes) smaller than vertexCount * stride ({} bytes).",
					update.vertexData.Size(), vertexBytes);
				return;
			}

			const u32 alignedSize = AlignMeshAllocSize(vertexBytes);
			if (alignedSize > mesh->vertexAllocSize)
			{
				FreeMeshData(mesh->vertexAlloc);
				mesh->vertexAlloc = AllocateMeshData(alignedSize);
				if (mesh->vertexAlloc.offset == OffsetAllocator::Allocation::NO_SPACE)
				{
					logger.Error("UpdateProceduralMesh '{}': out of mesh-data buffer space for vertex grow.", mesh->debugName);
					mesh->vertexByteOffset = U32_MAX;
					mesh->vertexAllocSize  = 0;
					return;
				}
				mesh->vertexByteOffset = mesh->vertexAlloc.offset;
				mesh->vertexAllocSize  = alignedSize;
			}
			mesh->vertexCount = update.vertexCount;
		}

		if (hasIndexUpdate)
		{
			const u32 indexBytes  = static_cast<u32>(update.indices.Size() * sizeof(u32));
			const u32 alignedSize = AlignMeshAllocSize(indexBytes);
			if (alignedSize > mesh->indexAllocSize)
			{
				FreeMeshData(mesh->indexAlloc);
				mesh->indexAlloc = AllocateMeshData(alignedSize);
				if (mesh->indexAlloc.offset == OffsetAllocator::Allocation::NO_SPACE)
				{
					logger.Error("UpdateProceduralMesh '{}': out of mesh-data buffer space for index grow.", mesh->debugName);
					mesh->indexByteOffset = U32_MAX;
					mesh->indexAllocSize  = 0;
					return;
				}
				mesh->indexByteOffset = mesh->indexAlloc.offset;
				mesh->indexAllocSize  = alignedSize;
			}
		}

		if (hasPrimUpdate)
		{
			const u32 oldCount = static_cast<u32>(mesh->primitiveInfoSlots.Size());
			const u32 newCount = static_cast<u32>(update.primitives.Size());

			for (u32 i = newCount; i < oldCount; ++i)
			{
				FreePrimitiveInfoSlot(mesh->primitiveInfoSlots[i]);
			}
			mesh->primitiveInfoSlots.Resize(newCount);
			for (u32 i = oldCount; i < newCount; ++i)
			{
				mesh->primitiveInfoSlots[i] = AllocatePrimitiveInfoSlot();
			}

			mesh->primitives.Resize(newCount);
			for (u32 p = 0; p < newCount; ++p) mesh->primitives[p] = update.primitives[p];

			const u32 indexBaseUnits = mesh->indexByteOffset / static_cast<u32>(sizeof(u32));
			for (u32 p = 0; p < newCount; ++p)
			{
				GpuMeshPrimitiveInfo info{};
				info.lodCount = 1;
				info.lods[0].firstIndex = indexBaseUnits + update.primitives[p].firstIndex;
				info.lods[0].indexCount = update.primitives[p].indexCount;
				info.lods[0].screenSize = 0.0f;
				WriteMeshPrimitiveInfo(mesh->primitiveInfoSlots[p], info);
			}
		}
		else if (hasIndexUpdate)
		{
			const u32 indexBaseUnits = mesh->indexByteOffset / static_cast<u32>(sizeof(u32));
			for (u32 p = 0; p < mesh->primitives.Size(); ++p)
			{
				GpuMeshPrimitiveInfo info{};
				info.lodCount = 1;
				info.lods[0].firstIndex = indexBaseUnits + mesh->primitives[p].firstIndex;
				info.lods[0].indexCount = mesh->primitives[p].indexCount;
				info.lods[0].screenSize = 0.0f;
				WriteMeshPrimitiveInfo(mesh->primitiveInfoSlots[p], info);
			}
		}

		if (update.aabbValid)
		{
			mesh->aabb = update.aabb;
		}

		if (!hasVertexUpdate && !hasIndexUpdate && !(update.rebuildBlas && mesh->wantsBlas))
		{
			return;
		}

		ResourceWorker::WorkerData wd;
		wd.resourceCache = mesh;

		if (hasVertexUpdate)
		{
			const u32 vertexBytes = update.vertexCount * stride;
			wd.proceduralVertexData.Resize(vertexBytes);
			memcpy(wd.proceduralVertexData.begin(), update.vertexData.begin(), vertexBytes);
			wd.proceduralVertexCount = update.vertexCount;
		}
		else
		{
			wd.proceduralVertexCount = 0;
		}

		if (hasIndexUpdate)
		{
			const u32 indexBytes = static_cast<u32>(update.indices.Size() * sizeof(u32));
			wd.proceduralIndexData.Resize(indexBytes);
			memcpy(wd.proceduralIndexData.begin(), update.indices.begin(), indexBytes);
		}

		wd.proceduralDstVertexOffset = hasVertexUpdate ? mesh->vertexByteOffset : U32_MAX;
		wd.proceduralDstIndexOffset  = hasIndexUpdate  ? mesh->indexByteOffset  : U32_MAX;
		wd.proceduralStride          = stride;

		const bool rebuildBlas = update.rebuildBlas && mesh->wantsBlas;
		if (rebuildBlas)
		{
			if (!hasVertexUpdate)
			{
				wd.proceduralVertexCount     = mesh->vertexCount;
				wd.proceduralDstVertexOffset = mesh->vertexByteOffset;
			}
			if (!hasIndexUpdate)
			{
				wd.proceduralDstIndexOffset = mesh->indexByteOffset;
			}
			wd.proceduralPrimitives.Resize(mesh->primitives.Size());
			for (u32 p = 0; p < mesh->primitives.Size(); ++p) wd.proceduralPrimitives[p] = mesh->primitives[p];
			wd.proceduralBuildBlas = true;
		}

		worker->AddProceduralMeshTask(std::move(wd), false, false);
	}

	i32& RenderDebug::ForcedLod()
	{
		static i32 value = -1;
		return value;
	}

	GPUDescriptorSet* RenderResourceCache::GetGlobalDescriptorSet()
	{
		return globalDescriptorSet;
	}

	GPUBuffer* RenderResourceCache::GetMaterialDataBuffer()
	{
		return materialDataBuffer;
	}

	u32 RenderResourceCache::GetMaterialDataCount()
	{
		return materialDataCount;
	}

	GPUBuffer* RenderResourceCache::GetMeshDataBuffer()
	{
		return meshDataBuffer;
	}

	u64 RenderResourceCache::GetMeshReloadVersion()
	{
		return meshReloadVersion;
	}

	bool RenderResourceCache::GetMeshPrimitiveBlasLod(const MeshResourceCachePtr& mesh, u32 primitiveIndex, u32& firstIndex, u32& indexCount)
	{
		if (!mesh || primitiveIndex >= mesh->primitives.Size() || mesh->indexByteOffset == U32_MAX)
		{
			return false;
		}

		const MeshPrimitive& primitive = mesh->primitives[primitiveIndex];
		firstIndex = primitive.firstIndex + static_cast<u32>(mesh->indexByteOffset / sizeof(u32));
		indexCount = primitive.indexCount;

		std::scoped_lock lock(primitiveInfoMutex);
		const GpuMeshPrimitiveInfo* lodInfoBuffer =
			meshPrimitiveInfoBuffer
				? static_cast<const GpuMeshPrimitiveInfo*>(meshPrimitiveInfoBuffer->GetMappedData())
				: nullptr;

		if (lodInfoBuffer && primitiveIndex < mesh->primitiveInfoSlots.Size())
		{
			u32 slot = mesh->primitiveInfoSlots[primitiveIndex];
			if (slot != U32_MAX)
			{
				const GpuMeshPrimitiveInfo& info = lodInfoBuffer[slot];
				if (info.lodCount > 0)
				{
					u32 lodIdx = std::min(BlasLod, info.lodCount - 1);
					firstIndex = info.lods[lodIdx].firstIndex;
					indexCount = info.lods[lodIdx].indexCount;
				}
			}
		}

		return indexCount > 0;
	}

	SkinResourceCachePtr RenderResourceCache::GetSkinCache(RID mesh)
	{
		if (!mesh) return nullptr;

		std::unique_lock lock(skinCacheMutex);
		auto             it = skinCache.Find(mesh);
		if (it != skinCache.end())
		{
			if (auto sp = it->second.lock()) return sp;
			skinCache.Erase(it);
		}

		ResourceObject meshObject = Resources::Read(mesh);
		if (!meshObject) return nullptr;

		RID skin = meshObject.GetSubObject(MeshResource::Skin);
		if (!skin) return nullptr;

		SkinResourceCachePtr skinCacheData(Alloc<SkinResourceCache>(skin, mesh), MakeCacheDeleter<SkinResourceCache>(skinCache, skinCacheMutex, mesh));

		if (ResourceObject skinObject = Resources::Read(skin))
		{
			skinCacheData->poses.Reserve(skinObject.GetSubObjectListCount(SkinResource::Binds));

			for (RID bind : skinObject.GetSubObjectList(SkinResource::Binds))
			{
				ResourceObject bindObject = Resources::Read(bind);
				skinCacheData->poses.EmplaceBack(bindObject.GetMat4(SkinBindResource::Pose));
			}
		}

		skinCache.Insert(mesh, std::weak_ptr(skinCacheData));
		return skinCacheData;
	}

	void RenderResourceCacheInit()
	{
		globalDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::StorageBuffer
				},
				DescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = DescriptorType::StorageBuffer
				},
				DescriptorSetLayoutBinding{
					.binding = 2,
					.count = 4,
					.descriptorType = DescriptorType::Sampler
				},
				DescriptorSetLayoutBinding{
					.binding = 3,
					.descriptorType = DescriptorType::SampledImage,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 4,
					.descriptorType = DescriptorType::StorageBuffer
				},
				DescriptorSetLayoutBinding{
					.binding = 5,
					.descriptorType = DescriptorType::UniformBuffer,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 6,
					.descriptorType = DescriptorType::StorageBuffer
				},
				DescriptorSetLayoutBinding{
					.binding = 7,
					.descriptorType = DescriptorType::StorageBuffer
				},
			},
			.debugName = "GlobalDescriptorSet"
		});

		globalDescriptorSet->UpdateSampler(2, Graphics::GetLinearSampler(), 0);
		globalDescriptorSet->UpdateSampler(2, Graphics::GetNearestSampler(), 1);
		globalDescriptorSet->UpdateSampler(2, Graphics::GetLinearClampToEdgeSampler(), 2);
		globalDescriptorSet->UpdateSampler(2, Graphics::GetNearestClampToEdgeSampler(), 3);
		globalDescriptorSetAlive = true;

		meshDataBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = MeshDataBufferSize,
			.usage = ResourceUsage::CopyDest | ResourceUsage::VertexBuffer | ResourceUsage::IndexBuffer | ResourceUsage::ShaderResource | ResourceUsage::AccelerationStructure,
			.hostVisible = false,
			.persistentMapped = false,
			.debugName = "MeshDataBuffer"
		});

		meshDataAllocator = std::make_unique<OffsetAllocator::Allocator>(MeshDataBufferSize, MeshDataMaxAllocations);

		globalDescriptorSet->UpdateBuffer(4, meshDataBuffer, 0, MeshDataBufferSize);

		EnsurePrimitiveInfoCapacity(1024);

		worker = std::make_unique<ResourceWorker>(4);
	}

	void RenderResourceCacheShutdown()
	{
		fontCache.Clear();
		textureAsyncCache.Clear();
		textureCache.Clear();
		materialCache.Clear();
		meshCache.Clear();
		skinCache.Clear();

		{
			std::scoped_lock lock(pendingMaterialsMutex);
			pendingMaterials.Clear();
		}

		globalDescriptorSetAlive = false;

		if (globalDescriptorSet)
		{
			globalDescriptorSet->Destroy();
			globalDescriptorSet = nullptr;
		}
		if (materialDataBuffer)
		{
			materialDataBuffer->Destroy();
			materialDataBuffer = nullptr;
		}

		if (materialParamBuffer)
		{
			materialParamBuffer->Destroy();
			materialParamBuffer = nullptr;
		}

		if (materialMaskBuffer)
		{
			materialMaskBuffer->Destroy();
			materialMaskBuffer = nullptr;
		}

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			if (materialMaskReadbackBuffers[f])
			{
				materialMaskReadbackBuffers[f]->Destroy();
				materialMaskReadbackBuffers[f] = nullptr;
			}
		}

		for (GPUBuffer* buffer : vertexLayoutBuffers)
		{
			buffer->Destroy();
		}
		vertexLayoutBuffers.Clear();
		vertexLayoutDescs.Clear();

		if (meshDataBuffer)
		{
			meshDataBuffer->Destroy();
			meshDataBuffer = nullptr;
		}
		meshDataAllocator.reset();

		if (meshPrimitiveInfoBuffer)
		{
			meshPrimitiveInfoBuffer->Destroy();
			meshPrimitiveInfoBuffer = nullptr;
		}
		meshPrimitiveInfoBufferCapacity = 0;
		meshPrimitiveInfoCount = 0;
		nextPrimitiveInfoSlot = 0;
		freePrimitiveInfoSlots.Clear();

		nextMaterialIndex = 0;
		nextTextureIndex = 0;
		freeMaterialIndices.Clear();
		freeTextureIndices.Clear();
		materialDataBufferCapacity = 0;
		materialDataCount = 0;
		materialParamBufferCapacity = 0;
		materialParamCount = 0;

		worker = {};
	}
}
