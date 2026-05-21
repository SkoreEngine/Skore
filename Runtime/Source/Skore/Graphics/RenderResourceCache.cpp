#include "Skore/Graphics/RenderResourceCache.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <semaphore>
#include <utility>

#include "concurrentqueue.h"
#include "Skore/App.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/DebugUtils.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	const u32 StagingBufferSize = 1024 * 1024 * 35;

	namespace
	{
		void ExecuteOnMainThread(std::function<void()> func);
		void WaitForTexture(const TextureResourceCachePtr& tex);
		void RegisterMeshBuffers(u32 geometryIndex, GPUBuffer* vertexBuffer, GPUBuffer* indexBuffer);

		enum class WorkerType : u32
		{
			None,
			Texture,
			Mesh,
			GenerateIBL
		};

		//TODO:   - CONCURRENT sharing on resources that cross queues
		//				- Add a VkSemaphore to the worker's transfer submit, and have the next graphics submit wait on it
		//				 (timeline semaphore is cleanest — one semaphore, monotonically increasing values, no per-frame pool).
		class ResourceWorker
		{
			struct WorkerData
			{
				WorkerType                          type = WorkerType::None;
				ResourceCachePtr                    resourceCache = {};
				std::shared_ptr<std::promise<void>> promise = nullptr;
			};

		public:
			void Init(u32 numWorkerThreads)
			{
				running.store(true, std::memory_order_release);
				for (u32 i = 0; i < numWorkerThreads; i++)
				{
					workerThreads.EmplaceBack(std::thread(&ResourceWorker::WorkerLoop, this));
				}
			}

			void Shutdown()
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

				while (running.load(std::memory_order_acquire))
				{
					workSem.acquire();

					if (!running.load(std::memory_order_acquire))
					{
						return;
					}

					WorkerData data;
					if (!load.try_dequeue(data))
					{
						// Spurious wake (e.g. shutdown release for an already-drained queue).
						continue;
					}
					pendingCount.fetch_sub(1, std::memory_order_release);

					//std::this_thread::sleep_for(std::chrono::milliseconds(1000));

					switch (data.type)
					{
						case WorkerType::Texture:
						{
							TextureResourceCachePtr textureCache = std::dynamic_pointer_cast<TextureResourceCache>(data.resourceCache);

							if (ResourceObject textureObject = Resources::Read(textureCache->rid))
							{
								u32             mipLevels = textureObject.GetUInt(TextureResource::MipLevels);
								TextureFormat   format = textureObject.GetEnum<TextureFormat>(TextureResource::Format);
								CompressionMode compressionMode = textureObject.GetEnum<CompressionMode>(TextureResource::CompressionMode);
								Span<RID>       images = textureObject.GetSubObjectList(TextureResource::Images);
								ResourceBuffer  buffer = textureObject.GetBuffer(TextureResource::PixelData);
								u32             texelSize = GetTextureFormatSize(format);

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
										transferCmd->ResourceBarrier(textureCache->texture, ResourceState::Undefined, ResourceState::CopyDest, 0, mipLevels, 0, 1);
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
											.texture = textureCache->texture,
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

								// Uploads one mip whose uncompressed size exceeds StagingBufferSize
								// by splitting it into row chunks. `srcReader` fills the staging buffer
								// with `byteCount` bytes starting at `srcByteOffset` within the mip.
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
											.texture = textureCache->texture,
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
									u32  mip = imageObject.GetUInt(TextureImageResource::Mip);
									u32  width = static_cast<u32>(extent.width);
									u32  height = static_cast<u32>(extent.height);
									u32  compressedSize = imageObject.GetUInt(TextureImageResource::DataSize);
									u32  uncompressedSize = imageObject.GetUInt(TextureImageResource::UncompressedSize);

									if (uncompressedSize > StagingBufferSize)
									{
										// Mip is bigger than the staging buffer — flush anything pending,
										// then upload this mip in row-chunked submissions.
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
								transferCmd->ResourceBarrier(textureCache->texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, mipLevels, 0, 1);
								transferCmd->End();
								transferQueue->SubmitAndWait(transferCmd);
								transferCmd->Reset();
							}
							break;
						}
						case WorkerType::Mesh:
						{
							MeshResourceCachePtr meshCache = std::dynamic_pointer_cast<MeshResourceCache>(data.resourceCache);

							ResourceObject meshObject = Resources::Read(meshCache->rid);
							if (!meshObject) break;

							// Calling thread pre-sized blasArray to signal RT intent. The worker
							// allocates the vertex/index buffers and (optionally) builds BLAS, then
							// publishes everything via the main-thread callback queue so readers
							// never observe a partially-constructed mesh cache.
							u32                      primitiveCount = static_cast<u32>(meshCache->blasArray.Size());
							bool                     wantsBlas = primitiveCount > 0;
							Array<GPUBottomLevelAS*> builtBlas;

							StringView     name = meshObject.GetString(MeshResource::Name);
							ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
							Span<RID>      lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);

							ResourceObject meshLodObject = Resources::Read(lods[0]);

							u64 vertexBufferOffset = meshLodObject.GetUInt(MeshLodResource::VerticesOffset);
							u32 vertexCount = static_cast<u32>(meshLodObject.GetUInt(MeshLodResource::VerticesCount));
							u64 vertexBufferSize = static_cast<u64>(vertexCount) * static_cast<u64>(meshCache->stride);
							u64 indexBufferOffset = meshLodObject.GetUInt(MeshLodResource::IndicesOffset);
							u64 indexBufferSize = meshLodObject.GetUInt(MeshLodResource::IndicesCount) * sizeof(u32);

							ResourceUsage rtUsage = wantsBlas ? ResourceUsage::AccelerationStructure | ResourceUsage::ShaderResource : ResourceUsage::None;
							GPUBuffer*    vertexBuffer = Graphics::CreateBuffer(BufferDesc{
								   .size = vertexBufferSize,
								   .usage = ResourceUsage::CopyDest | ResourceUsage::VertexBuffer | ResourceUsage::ShaderResource | rtUsage,
								   .hostVisible = false,
								   .persistentMapped = false,
								   .debugName = String(name) + "_VertexBuffer"
							});
							GPUBuffer* indexBuffer = Graphics::CreateBuffer(BufferDesc{
								.size = indexBufferSize,
								.usage = ResourceUsage::CopyDest | ResourceUsage::IndexBuffer | ResourceUsage::ShaderResource | rtUsage,
								.hostVisible = false,
								.persistentMapped = false,
								.debugName = String(name) + "_IndexBuffer"
							});

							// Upload one destination buffer (vertex or index). Chunks the copy through
							// the shared staging buffer when the source exceeds StagingBufferSize:
							// first chunk transitions Undefined→CopyDest, last chunk transitions
							// CopyDest→ShaderReadOnly, intermediate chunks only copy.
							auto uploadOne = [&](GPUBuffer* dst, u64 size, u64 srcOffset)
							{
								u64  done = 0;
								bool firstChunk = true;
								while (done < size)
								{
									u64 chunk = std::min<u64>(StagingBufferSize, size - done);
									buffer.CopyData(stagingBuffer->GetMappedData(), chunk, srcOffset + done);

									transferCmd->Begin();
									if (firstChunk)
									{
										transferCmd->ResourceBarrier(dst, ResourceState::Undefined, ResourceState::CopyDest);
										firstChunk = false;
									}
									transferCmd->CopyBuffer(stagingBuffer, dst, chunk, 0, done);
									if (done + chunk == size)
									{
										transferCmd->ResourceBarrier(dst, ResourceState::CopyDest, ResourceState::ShaderReadOnly);
									}
									transferCmd->End();
									transferQueue->SubmitAndWait(transferCmd);
									transferCmd->Reset();

									done += chunk;
								}
							};

							if (vertexBufferSize + indexBufferSize <= StagingBufferSize)
							{
								// Pack vertex + index into staging and upload in a single submission.
								u8* mapped = static_cast<u8*>(stagingBuffer->GetMappedData());
								buffer.CopyData(mapped, vertexBufferSize, vertexBufferOffset);
								buffer.CopyData(mapped + vertexBufferSize, indexBufferSize, indexBufferOffset);

								transferCmd->Begin();
								transferCmd->ResourceBarrier(vertexBuffer, ResourceState::Undefined, ResourceState::CopyDest);
								transferCmd->ResourceBarrier(indexBuffer, ResourceState::Undefined, ResourceState::CopyDest);
								transferCmd->CopyBuffer(stagingBuffer, vertexBuffer, vertexBufferSize, 0, 0);
								transferCmd->CopyBuffer(stagingBuffer, indexBuffer, indexBufferSize, vertexBufferSize, 0);
								transferCmd->ResourceBarrier(vertexBuffer, ResourceState::CopyDest, ResourceState::ShaderReadOnly);
								transferCmd->ResourceBarrier(indexBuffer, ResourceState::CopyDest, ResourceState::ShaderReadOnly);
								transferCmd->End();
								transferQueue->SubmitAndWait(transferCmd);
								transferCmd->Reset();
							}
							else
							{
								// Combined size doesn't fit — submit vertex and index separately,
								// reusing the staging buffer between submissions.
								uploadOne(vertexBuffer, vertexBufferSize, vertexBufferOffset);
								uploadOne(indexBuffer, indexBufferSize, indexBufferOffset);
							}

							// BLAS create + build, only when the calling thread pre-sized blasArray
							// (which it skips for skinned meshes or when ray tracing is disabled).
							// Worker builds into builtBlas locally; the main-thread publish callback
							// below moves it into meshCache->blasArray so readers never see partial state.
							if (wantsBlas)
							{
								builtBlas.Resize(primitiveCount, nullptr);

								Array<BottomLevelASDesc> blasDescs(primitiveCount);
								Array<GeometryDesc>      geometries(primitiveCount);
								u64                      maxScratch = 0;

								for (u32 p = 0; p < primitiveCount; ++p)
								{
									const MeshPrimitive& prim = meshCache->primitives[p];

									geometries[p] = GeometryDesc{};
									geometries[p].type = GeometryType::Triangles;
									geometries[p].triangles.vertexBuffer = vertexBuffer;
									geometries[p].triangles.vertexCount = vertexCount;
									geometries[p].triangles.vertexStride = meshCache->stride;
									geometries[p].triangles.vertexFormat = TextureFormat::R32G32B32_FLOAT;
									geometries[p].triangles.indexBuffer = indexBuffer;
									geometries[p].triangles.indexOffset = prim.firstIndex * sizeof(u32);
									geometries[p].triangles.indexCount = prim.indexCount;
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
									// Scratch is reused across primitives — serialize builds so each
									// finishes reading scratch before the next overwrites it.
									if (p + 1 < primitiveCount)
									{
										computeCmd->MemoryBarrier();
									}
								}
								computeCmd->End();
								computeQueue->SubmitAndWait(computeCmd);
								computeCmd->Reset();
							}

							// Hand the promise off to the publish callback so the future only resolves
							// after the buffers + descriptor slots + BLAS are visible on the main thread.
							// The catch-all promise-set at the bottom sees a moved-from (null) promise.
							auto pendingPromise = std::move(data.promise);
							ExecuteOnMainThread(
								[meshCache, vertexBuffer, indexBuffer, builtBlas = std::move(builtBlas), pendingPromise = std::move(pendingPromise)]() mutable
								{
									meshCache->vertexBuffer = vertexBuffer;
									meshCache->indexBuffer = indexBuffer;
									if (!builtBlas.Empty())
									{
										meshCache->blasArray = std::move(builtBlas);
									}
									// Register the buffers in the bindless storage slots (bindings 3/4)
									// at geometryIndex, so the descriptor never points at an
									// uninitialised buffer between cache creation and upload completion.
									RegisterMeshBuffers(meshCache->geometryIndex, vertexBuffer, indexBuffer);

									if (pendingPromise) pendingPromise->set_value();
								});
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

							GPUTexture* cubeMapTexture = Graphics::CreateTexture(TextureDesc{
								.extent = {256, 256, 1},
								.mipLevels = 8,
								.arrayLayers = 6,
								.format = TextureFormat::R16G16B16A16_FLOAT,
								.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
								.cubemap = true,
								.debugName = "SceneRendererViewport_CubemapTexture"
							});


							computeCmd->Begin();

							EquirectangularToCubeMap equirectangularToCubemap;
							equirectangularToCubemap.Init();
							equirectangularToCubemap.Execute(computeCmd, materialCache->skyMaterialTexture->texture, cubeMapTexture);

							SinglePassDownsampler downsampler;
							downsampler.Init(cubeMapTexture);
							downsampler.Execute(computeCmd);

							DiffuseIrradianceGenerator diffuseIrradianceGenerator;
							diffuseIrradianceGenerator.Init();
							diffuseIrradianceGenerator.Execute(computeCmd, cubeMapTexture, materialCache->diffuseIrradianceTexture);

							SpecularMapGenerator specularMapGenerator;
							specularMapGenerator.Init(cubeMapTexture, materialCache->specularMapTexture);
							specularMapGenerator.Execute(computeCmd);

							computeCmd->End();
							computeQueue->SubmitAndWait(computeCmd);
							computeCmd->Reset();

							equirectangularToCubemap.Destroy();
							diffuseIrradianceGenerator.Destroy();
							cubeMapTexture->Destroy();
							break;
						}
						default:
							break;
					}

					if (data.promise)
					{
						data.promise->set_value();
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


		Logger& logger = Logger::GetLogger("Skore.RenderResourceCache");

		// Fonts are kept strongly so per-frame DrawList lookups don't churn.
		HashMap<RID, FontResourceCachePtr>                 fontCache;
		HashMap<RID, std::weak_ptr<TextureResourceCache>>  textureCache;
		HashMap<RID, std::weak_ptr<MaterialResourceCache>> materialCache;
		HashMap<RID, std::weak_ptr<MeshResourceCache>>     meshCache;
		HashMap<RID, std::weak_ptr<SkinResourceCache>>     skinCache;

		std::mutex fontCacheMutex{};
		std::mutex materialCacheMutex{};
		std::mutex textureCacheMutex{};
		std::mutex meshCacheMutex{};
		std::mutex skinCacheMutex{};

		u32               nextGeometryIndex = 0;
		u32               nextMaterialIndex = 0;
		u32               nextTextureIndex = 0;
		Array<u32>        freeGeometryIndices;
		Array<u32>        freeMaterialIndices;
		Array<u32>        freeTextureIndices;
		GPUDescriptorSet* globalDescriptorSet = nullptr;
		GPUBuffer*        materialDataBuffer = nullptr;
		u32               materialDataBufferCapacity = 0;
		u32               materialDataCount = 0;
		bool              globalDescriptorSetAlive = false;

		ResourceWorker worker;

		std::mutex                        mainThreadTasksMutex;
		std::queue<std::function<void()>> mainThreadTasks;

		void ExecuteOnMainThread(std::function<void()> func)
		{
			std::scoped_lock lock(mainThreadTasksMutex);
			mainThreadTasks.emplace(std::move(func));
		}

		void RegisterMeshBuffers(u32 geometryIndex, GPUBuffer* vertexBuffer, GPUBuffer* indexBuffer)
		{
			DescriptorUpdate vtxUpdate;
			vtxUpdate.type = DescriptorType::StorageBuffer;
			vtxUpdate.binding = 3;
			vtxUpdate.arrayElement = geometryIndex;
			vtxUpdate.buffer = vertexBuffer;
			globalDescriptorSet->Update(vtxUpdate);

			DescriptorUpdate idxUpdate;
			idxUpdate.type = DescriptorType::StorageBuffer;
			idxUpdate.binding = 4;
			idxUpdate.arrayElement = geometryIndex;
			idxUpdate.buffer = indexBuffer;
			globalDescriptorSet->Update(idxUpdate);
		}

		u32 AllocateGeometryIndex()
		{
			if (!freeGeometryIndices.Empty())
			{
				u32 idx = freeGeometryIndices.Back();
				freeGeometryIndices.PopBack();
				return idx;
			}
			return nextGeometryIndex++;
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
			u32 pad0;
			u32 pad1;
			u32 pad2;
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
			f32 pad0;
			f32 pad1;
		};

		// Materials whose first-time index allocation is deferred until every referenced
		// texture has finished uploading — keeps materialIndex at U32_MAX so render passes
		// skip the drawcall and avoid sampling not-yet-uploaded textures.
		struct PendingMaterial
		{
			MaterialResourceCachePtr cache;
			MaterialData             data;
		};

		std::mutex           pendingMaterialsMutex;
		Array<PendingMaterial> pendingMaterials;

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

				materialDataBuffer = newBuffer;
				materialDataBufferCapacity = newCapacity;
			}

			MaterialData* mapped = static_cast<MaterialData*>(materialDataBuffer->GetMappedData());
			mapped[materialCache->materialIndex] = matData;

			materialDataCount = std::max(materialDataCount, materialCache->materialIndex + 1);

			globalDescriptorSet->UpdateBuffer(0, materialDataBuffer, 0, materialDataCount * sizeof(MaterialData));
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

		void UpdateMaterialStorageData(const ResourceObject& materialObject, MaterialResourceCachePtr materialCache, bool async)
		{
			StringView name = materialObject.GetString(MaterialResource::Name);

			materialCache->type = materialObject.GetEnum<MaterialResource::MaterialType>(MaterialResource::Type);
			materialCache->shader = materialObject.GetReference(MaterialResource::Shader);

			// Drop previous texture references; rebuild based on the new material data.
			materialCache->textures.Clear();

			if (materialCache->type == MaterialResource::MaterialType::Opaque)
			{
				Color baseColor = materialObject.GetColor(MaterialResource::BaseColor);
				RID   baseColorTexture = materialObject.GetReference(MaterialResource::BaseColorTexture);
				RID   normalTexture = materialObject.GetReference(MaterialResource::NormalTexture);
				RID   roughnessTexture = materialObject.GetReference(MaterialResource::RoughnessTexture);
				RID   metallicTexture = materialObject.GetReference(MaterialResource::MetallicTexture);
				Color emissiveColor = materialObject.GetColor(MaterialResource::EmissiveColor);
				f32   emissiveFactor = materialObject.GetFloat(MaterialResource::EmissiveFactor);
				RID   emissiveTexture = materialObject.GetReference(MaterialResource::EmissiveTexture);

				auto resolveTextureIndex = [&materialCache, async](RID textureRID) -> i32
				{
					if (!textureRID) return -1;
					TextureResourceCachePtr tc = RenderResourceCache::GetTextureCache(textureRID, async);
					if (tc && tc->textureIndex != U32_MAX)
					{
						i32 idx = static_cast<i32>(tc->textureIndex);
						materialCache->textures.EmplaceBack(Traits::Move(tc));
						return idx;
					}
					return -1;
				};

				MaterialData matData{};
				matData.baseColor = baseColor.ToVec3();
				matData.roughness = materialObject.GetFloat(MaterialResource::Roughness);
				matData.metallic = materialObject.GetFloat(MaterialResource::Metallic);
				matData.baseColorTexture = resolveTextureIndex(baseColorTexture);
				matData.normalTexture = resolveTextureIndex(normalTexture);
				matData.roughnessTexture = resolveTextureIndex(roughnessTexture);
				matData.metallicTexture = resolveTextureIndex(metallicTexture);
				matData.uvScale = materialObject.GetVec2(MaterialResource::UvScale);
				matData.emissiveFactor = emissiveFactor;
				matData.emissiveColor = emissiveColor.ToVec3();
				matData.emissiveTexture = resolveTextureIndex(emissiveTexture);
				matData.alphaMode = materialObject.GetEnum(MaterialResource::AlphaMode);
				matData.alphaCutoff = materialObject.GetFloat(MaterialResource::AlphaCutoff);

				materialCache->transparent = materialObject.GetEnum<MaterialResource::MaterialAlphaMode>(MaterialResource::AlphaMode) == MaterialResource::MaterialAlphaMode::Blend;

				// First-time creation: defer index allocation + GPU write until every texture has
				// finished uploading. Flush() promotes pending entries once they're ready, so render
				// passes (which skip drawcalls whose materialIndex is U32_MAX) avoid sampling
				// partially-uploaded textures and flashing garbage.
				// Reload of an already-published material rewrites in-place so we don't drop the slot.
				if (materialCache->materialIndex == U32_MAX)
				{
					AddPendingMaterial(materialCache, matData);
				}
				else
				{
					WriteMaterialDataToBuffer(materialCache, matData);
				}
			}
			else if (materialCache->type == MaterialResource::MaterialType::SkyboxEquirectangular)
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
							.format = TextureFormat::R16G16B16A16_FLOAT,
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
							.format = TextureFormat::R16G16B16A16_FLOAT,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
							.cubemap = true,
							.debugName = "SceneRendererViewport_SpecularMapTexture"
						});
					}

					materialCache->iblComplete = worker.AddTask(WorkerType::GenerateIBL, materialCache).share();
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

		// Block until any worker task tied to the given cache (and its dependents)
		// has signalled completion. Cheap no-op when the future is already ready
		// or was never populated.
		void WaitForTexture(const TextureResourceCachePtr& tex)
		{
			if (tex && tex->uploadComplete.valid())
			{
				tex->uploadComplete.wait();
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
		if (textureIndex != U32_MAX)
		{
			if (globalDescriptorSetAlive && globalDescriptorSet)
			{
				DescriptorUpdate texUpdate;
				texUpdate.type = DescriptorType::SampledImage;
				texUpdate.binding = 2;
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
			}
			eventRegistered = false;
		}

		if (materialIndex != U32_MAX)
		{
			freeMaterialIndices.EmplaceBack(materialIndex);
			materialIndex = U32_MAX;
		}

		if (descriptorSet) descriptorSet->Destroy();
		if (materialBuffer) materialBuffer->Destroy();
		if (diffuseIrradianceTexture) diffuseIrradianceTexture->Destroy();
		if (specularMapTexture) specularMapTexture->Destroy();
	}

	MeshResourceCache::~MeshResourceCache()
	{
		if (geometryIndex != U32_MAX)
		{
			freeGeometryIndices.EmplaceBack(geometryIndex);
			geometryIndex = U32_MAX;
		}

		for (GPUBottomLevelAS* blas : blasArray)
		{
			if (blas) blas->Destroy();
		}
		blasArray.Clear();

		if (vertexBuffer) vertexBuffer->Destroy();
		if (indexBuffer) indexBuffer->Destroy();
	}

	bool RenderResourceCache::WorkerIdle()
	{
		return worker.Idle();
	}

	void RenderResourceCache::Flush()
	{
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

		// Promote deferred materials whose textures finished uploading this frame.
		// Non-blocking — entries that aren't ready stay in the pending list.
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
			.format = TextureFormat::R32G32B32A32_FLOAT,
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

		TextureResourceCachePtr result;
		{
			std::unique_lock lock(textureCacheMutex);
			auto             it = textureCache.Find(texture);
			if (it != textureCache.end())
			{
				if (auto sp = it->second.lock())
				{
					result = sp;
				}
				else
				{
					textureCache.Erase(it);
				}
			}

			if (!result)
			{
				TextureResourceCachePtr textureStorage(Alloc<TextureResourceCache>(texture), MakeCacheDeleter<TextureResourceCache>(textureCache, textureCacheMutex, texture));

				if (ResourceObject textureObject = Resources::Read(texture))
				{
					StringView name = textureObject.GetString(TextureResource::Name);
					TextureFormat format = textureObject.GetEnum<TextureFormat>(TextureResource::Format);
					Span<RID> images = textureObject.GetSubObjectList(TextureResource::Images);

					ResourceObject firstImageObject = Resources::Read(images[0]);
					Vec2 extent = firstImageObject.GetVec2(TextureImageResource::Extent);
					u32 mipLevels = textureObject.GetUInt(TextureResource::MipLevels);


					//create texture
					textureStorage->texture = Graphics::CreateTexture(TextureDesc{
						.extent = {static_cast<u32>(extent.x), static_cast<u32>(extent.y), 1},
						.mipLevels = std::max(mipLevels, 1u),
						.format = format,
						.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
						.debugName = String(name) + "_Texture"
					});

					Graphics::SetTextureState(textureStorage->texture, ResourceState::Undefined, ResourceState::ShaderReadOnly);

					textureStorage->uploadComplete = worker.AddTask(WorkerType::Texture, textureStorage).share();

					//update index.
					textureStorage->textureIndex = AllocateTextureIndex();

					DescriptorUpdate texUpdate;
					texUpdate.type = DescriptorType::SampledImage;
					texUpdate.binding = 2;
					texUpdate.arrayElement = textureStorage->textureIndex;
					texUpdate.texture = textureStorage->texture;
					globalDescriptorSet->Update(texUpdate);
				}

				textureCache.Insert(texture, std::weak_ptr(textureStorage));
				result = textureStorage;
			}
		}

		if (!async && result && result->uploadComplete.valid())
		{
			result->uploadComplete.wait();
		}
		return result;
	}

	MaterialResourceCachePtr RenderResourceCache::GetMaterialCache(RID material, bool async)
	{
		if (!material) return nullptr;

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
				MaterialResourceCachePtr materialData(Alloc<MaterialResourceCache>(material), MakeCacheDeleter<MaterialResourceCache>(materialCache, materialCacheMutex, material));

				if (ResourceStorage* storage = Resources::GetStorage(material))
				{
					storage->RegisterEvent(ResourceEventType::Changed, GraphicsResourceStorageMaterialReload, nullptr);
					materialData->eventRegistered = true;
				}

				ResourceObject materialObject = Resources::Read(material);
				UpdateMaterialStorageData(materialObject, materialData, async);

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

		MeshResourceCachePtr meshData(Alloc<MeshResourceCache>(mesh), MakeCacheDeleter<MeshResourceCache>(meshCache, meshCacheMutex, mesh));

		if (ResourceObject meshObject = Resources::Read(mesh))
		{
			//ScopedTimer timer(logger, " time to load mesh ");

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
						}
					}
				}
			}
			Span<RID> materials = meshObject.GetReferenceArray(MeshResource::Materials);

			ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
			Span<RID>      lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);

			ResourceObject meshLodObject = Resources::Read(lods[0]);

			u64 primitiveOffset = meshLodObject.GetUInt(MeshLodResource::PrimitiveOffset);
			u64 primitiveCount = meshLodObject.GetUInt(MeshLodResource::PrimitiveCount);
			u64 primitiveSize = primitiveCount * sizeof(MeshPrimitive);

			bool rtSupported = Graphics::GetDevice()->GetFeatures().rayTracing;

			meshData->primitives.Resize(primitiveCount);
			buffer.CopyData(meshData->primitives.Data(), primitiveSize, primitiveOffset);

			meshData->geometryIndex = AllocateGeometryIndex();

			VertexLayoutOffsetData layoutData{};
			layoutData.stride         = meshData->stride;
			layoutData.posOff         = positionOffset;
			layoutData.normalOff      = normalOffset;
			layoutData.uvOff          = uvOffset;
			layoutData.uv1Off         = uv1Offset;
			layoutData.colorOff       = colorOffset;
			layoutData.tangentOff     = tangentOffset;
			layoutData.boneIndicesOff = boneIndicesOffset;
			layoutData.boneWeightsOff = boneWeightsOffset;

			meshData->vertexLayoutId = FindOrCreateVertexLayout(layoutData);

			// Pre-size the BLAS slot array on the calling thread as the worker's RT intent
			// signal (skipped for skinned meshes). The worker creates vertex/index buffers
			// and BLAS handles, then publishes them — along with the bindless descriptor
			// updates for slots 3/4 at geometryIndex — via the main-thread callback queue.
			if (rtSupported && !meshObject.GetSubObject(MeshResource::Skin))
			{
				meshData->blasArray.Resize(primitiveCount, nullptr);
			}

			meshData->uploadComplete = worker.AddTask(WorkerType::Mesh, meshData).share();

			if (!materials.Empty())
			{
				meshData->materials.Reserve(materials.Size());
				for (usize i = 0; i < materials.Size(); i++)
				{
					meshData->materials.EmplaceBack(GetMaterialCache(materials[i], async));
				}
			}
			else
			{
				meshData->materials.EmplaceBack(GetMaterialCache(Resources::FindByPath("Skore://Materials/DefaultMaterial.material"), async));
			}
		}

		meshCache.Insert(mesh, std::weak_ptr<MeshResourceCache>(meshData));
		lock.unlock();
		if (!async) WaitForMesh(meshData);
		return meshData;
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
					.descriptorType = DescriptorType::Sampler
				},
				DescriptorSetLayoutBinding{
					.binding = 2,
					.descriptorType = DescriptorType::SampledImage,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 3,
					.descriptorType = DescriptorType::StorageBuffer,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 4,
					.descriptorType = DescriptorType::StorageBuffer,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 5,
					.descriptorType = DescriptorType::UniformBuffer,
					.renderType = RenderType::RuntimeArray
				},
			},
			.debugName = "GlobalDescriptorSet"
		});

		globalDescriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());
		globalDescriptorSetAlive = true;

		worker.Init(4);
	}

	void RenderResourceCacheShutdown()
	{
		// Drop map entries. With weak_ptr storage this only frees the weak slot;
		// any cache that still has outstanding shared_ptrs will outlive shutdown
		// and its dtor will skip global-descriptor cleanup once the flag flips.
		fontCache.Clear();
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

		for (GPUBuffer* buffer : vertexLayoutBuffers)
		{
			buffer->Destroy();
		}
		vertexLayoutBuffers.Clear();
		vertexLayoutDescs.Clear();

		nextGeometryIndex = 0;
		nextMaterialIndex = 0;
		nextTextureIndex = 0;
		freeGeometryIndices.Clear();
		freeMaterialIndices.Clear();
		freeTextureIndices.Clear();
		materialDataBufferCapacity = 0;
		materialDataCount = 0;

		worker.Shutdown();
	}
}
