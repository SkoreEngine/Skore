#include "RenderGraph.hpp"

#include "Graphics.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/Allocator.hpp"

namespace Skore
{
	namespace
	{
		void HashCombine(usize& seed, usize value)
		{
			seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		}

		ResourceState PassTargetState(RenderGraphPassType type, RenderGraphAccess access, bool isDepth)
		{
			switch (type)
			{
				case RenderGraphPassType::Graphics:
					if (access == RenderGraphAccess::Read)
					{
						return isDepth ? ResourceState::DepthStencilReadOnly : ResourceState::ShaderReadOnly;
					}
					return isDepth ? ResourceState::DepthStencilAttachment : ResourceState::ColorAttachment;
				case RenderGraphPassType::Compute:
				case RenderGraphPassType::Raytrace:
					return access == RenderGraphAccess::Read ? ResourceState::ShaderReadOnly : ResourceState::General;
				case RenderGraphPassType::Transfer:
					if (access == RenderGraphAccess::Read) return ResourceState::CopySource;
					if (access == RenderGraphAccess::Write) return ResourceState::CopyDest;
					return ResourceState::General;
			}
			return ResourceState::General;
		}

		ResourceState PassBufferTargetState(RenderGraphPassType type, RenderGraphAccess access)
		{
			if (type == RenderGraphPassType::Transfer)
			{
				if (access == RenderGraphAccess::Read) return ResourceState::CopySource;
				if (access == RenderGraphAccess::Write) return ResourceState::CopyDest;
				return ResourceState::General;
			}

			return access == RenderGraphAccess::Read ? ResourceState::ShaderReadOnly : ResourceState::General;
		}

		BarrierSyncScope PassSyncScope(RenderGraphPassType type)
		{
			switch (type)
			{
				case RenderGraphPassType::Graphics:
					return BarrierSyncScope::Graphics;
				case RenderGraphPassType::Compute:
					return BarrierSyncScope::Compute;
				case RenderGraphPassType::Raytrace:
					return BarrierSyncScope::Raytrace;
				case RenderGraphPassType::Transfer:
					return BarrierSyncScope::Transfer;
			}
			return BarrierSyncScope::Automatic;
		}

		u32 TextureMipCount(GPUTexture* texture)
		{
			const TextureDesc& desc = texture->GetDesc();
			return desc.mipLevels != 0 ? desc.mipLevels : 1;
		}

		u32 TextureLayerCount(GPUTexture* texture)
		{
			const TextureDesc& desc = texture->GetDesc();
			return desc.arrayLayers != 0 ? desc.arrayLayers : 1;
		}

		u32 SubresourceIndex(u32 mipLevel, u32 arrayLayer, u32 arrayLayers)
		{
			return mipLevel * arrayLayers + arrayLayer;
		}

		bool ReadsResource(RenderGraphAccess access)
		{
			return access == RenderGraphAccess::Read || access == RenderGraphAccess::ReadWrite;
		}

		bool WritesResource(RenderGraphAccess access)
		{
			return access == RenderGraphAccess::Write || access == RenderGraphAccess::ReadWrite;
		}

		void AddPassEdge(Array<Array<u32>>& edges, Array<u32>& indegrees, u32 from, u32 to)
		{
			if (from == to) return;

			for (u32 existing : edges[from])
			{
				if (existing == to)
				{
					return;
				}
			}

			edges[from].EmplaceBack(to);
			++indegrees[to];
		}
	}

	RenderGraph::RenderGraph(GPUDevice* device) : device(device) {}

	GPUDevice* RenderGraph::GetDevice() const
	{
		return device;
	}

	RenderGraph::~RenderGraph()
	{
		for (RenderGraphPass* pass : passes)
		{
			DestroyAndFree(pass);
		}
		passes.Clear();

		for (RenderGraphPass* pass : passPool)
		{
			DestroyAndFree(pass);
		}
		passPool.Clear();

		for (auto& it : resources)
		{
			if (it.second.instanceData)
			{
				operator delete(it.second.instanceData);
				it.second.instanceData = nullptr;
			}
		}
	}

	RenderGraphPass& RenderGraphPass::Write(StringView name)
	{
		dependencies.EmplaceBack(Dependency{.name = name, .access = RenderGraphAccess::Write});
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Read(StringView name)
	{
		dependencies.EmplaceBack(Dependency{.name = name, .access = RenderGraphAccess::Read});
		return *this;
	}

	RenderGraphPass& RenderGraphPass::WriteRead(StringView name)
	{
		dependencies.EmplaceBack(Dependency{.name = name, .access = RenderGraphAccess::ReadWrite});
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Resolve(StringView name)
	{
		resolves.EmplaceBack(name);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::DescriptorSet(u32 set, GPUDescriptorSet* ds)
	{
		boundDescriptorSets.Insert(set, ds);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::InvertViewport(bool value)
	{
		invertViewport = value;
		return *this;
	}

	RenderGraphPass& RenderGraphPass::RequireJitter(bool value)
	{
		requireJitter = value;
		return *this;
	}

	RenderGraphPass& RenderGraphPass::RequireMotionVector(bool value)
	{
		requireMotionVector = value;
		return *this;
	}

	RenderGraphPass& RenderGraphPass::ClearColor(StringView name, const Vec4& color)
	{
		if (RenderGraph::Resource* resource = graph->FindResource(name))
		{
			resource->textureDesc.clearColor = color;
		}
		return *this;
	}

	RenderGraphPass& RenderGraphPass::SetConstants(u32 size, ShaderStage stages, std::function<void(RenderGraph&, void*)> f)
	{
		constantsSize = size;
		constantsStages = stages;
		constantsFn = Traits::Move(f);
		return *this;
	}

	void RenderGraphPass::Reset(RenderGraph* graph, StringView name, RenderGraphPassType type)
	{
		this->graph = graph;
		this->name = name;
		this->type = type;
		shader = {};
		pipeline = nullptr;
		renderPass = nullptr;

		dependencies.Clear();
		resolves.Clear();
		boundDescriptorSets.Clear();

		invertViewport = false;
		requireJitter = false;
		requireMotionVector = false;

		dispatchX = 0;
		dispatchY = 0;
		dispatchZ = 0;
		indirectBuffer = nullptr;

		constantsSize = 0;
		constantsStages = ShaderStage::All;
		constantsFn = {};
		resizeFn = {};
		renderFn = {};
	}

	RenderGraphPass& RenderGraphPass::Resize(std::function<void(RenderGraph& rg, Extent newExtent)> f)
	{
		resizeFn = Traits::Move(f);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Render(std::function<void(RenderGraph& rg, Scene* scene, GPUCommandBuffer* cmd)> fn)
	{
		renderFn = Traits::Move(fn);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Dispatch(u32 x, u32 y, u32 z)
	{
		dispatchX = x;
		dispatchY = y;
		dispatchZ = z;
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Dispatch(Extent3D extent)
	{
		return Dispatch(extent.width, extent.height, extent.depth);
	}

	RenderGraphPass& RenderGraphPass::DispatchIndirect(GPUBuffer* buffer)
	{
		indirectBuffer = buffer;
		return *this;
	}

	RenderGraphPass& RenderGraphPass::TraceRays(u32 width, u32 height, u32 depth)
	{
		dispatchX = width;
		dispatchY = height;
		dispatchZ = depth;
		return *this;
	}

	GPUPipeline* RenderGraphPass::GetPipeline() const
	{
		return pipeline;
	}

	GPURenderPass* RenderGraphPass::GetRenderPass() const
	{
		return renderPass;
	}

	RenderGraphPass& RenderGraph::EmplacePass(StringView name, RenderGraphPassType type)
	{
		RenderGraphPass* pass;
		if (!passPool.Empty())
		{
			pass = passPool.Back();
			passPool.PopBack();
		}
		else
		{
			pass = Alloc<RenderGraphPass>();
		}

		pass->Reset(this, name, type);
		passes.EmplaceBack(pass);
		passesSorted = false;
		return *pass;
	}

	RenderGraph::Resource* RenderGraph::FindResource(StringView name)
	{
		auto it = resources.Find(name);
		return it != resources.end() ? &it->second : nullptr;
	}

	const RenderGraph::Resource* RenderGraph::FindResource(StringView name) const
	{
		auto it = resources.Find(name);
		return it != resources.end() ? &it->second : nullptr;
	}

	void RenderGraph::Create(StringView name, const RenderGraphTextureDesc& textureDesc)
	{
		if (Resource* existing = FindResource(name))
		{
			existing->textureDesc = textureDesc;
			return;
		}

		Resource resource;
		resource.kind = Resource::Kind::Texture;
		resource.textureDesc = textureDesc;
		resources.Insert(name, Traits::Move(resource));
		resourcesDirty = true;
	}

	void RenderGraph::Create(StringView name, const RenderGraphBufferDesc& bufferDesc)
	{
		if (Resource* existing = FindResource(name))
		{
			existing->bufferDesc = bufferDesc;
			return;
		}

		Resource resource;
		resource.kind = Resource::Kind::Buffer;
		resource.bufferDesc = bufferDesc;
		resources.Insert(name, Traits::Move(resource));
		resourcesDirty = true;
	}

	void RenderGraph::CreateView(StringView name, const RenderGraphViewDesc& viewDesc)
	{
		if (Resource* existing = FindResource(name))
		{
			existing->viewDesc = viewDesc;
			return;
		}

		Resource resource;
		resource.kind = Resource::Kind::View;
		resource.viewDesc = viewDesc;
		resources.Insert(name, Traits::Move(resource));
		resourcesDirty = true;
	}

	void RenderGraph::Import(StringView name, Span<GPUTexture*> textures, ResourceState state)
	{
		Resource* resource = FindResource(name);
		if (resource == nullptr)
		{
			Resource newResource;
			newResource.kind = Resource::Kind::Imported;
			resources.Insert(name, Traits::Move(newResource));
			resource = FindResource(name);
		}

		resource->kind = Resource::Kind::Imported;
		resource->imported.Clear();
		resource->imported.Insert(resource->imported.end(), textures.begin(), textures.end());
		resource->importedState = state;
		resource->importedStates.Clear();
		resource->importedScopes.Clear();
		resource->importedLastWrites.Clear();

		for (GPUTexture* texture : resource->imported)
		{
			Array<ResourceState> states;
			Array<BarrierSyncScope> scopes;
			Array<bool>         lastWrites;
			if (texture != nullptr)
			{
				states.Resize(TextureMipCount(texture) * TextureLayerCount(texture), state);
				scopes.Resize(states.Size(), BarrierSyncScope::Automatic);
				lastWrites.Resize(states.Size(), false);
			}
			resource->importedStates.EmplaceBack(Traits::Move(states));
			resource->importedScopes.EmplaceBack(Traits::Move(scopes));
			resource->importedLastWrites.EmplaceBack(Traits::Move(lastWrites));
		}
	}

	VoidPtr RenderGraph::CreateInstance(StringView name, usize size)
	{
		if (Resource* existing = FindResource(name))
		{
			if (existing->instanceData && existing->instanceSize == size)
			{
				return existing->instanceData;
			}
		}

		Resource resource;
		resource.kind = Resource::Kind::Instance;
		resource.instanceSize = size;
		resource.instanceData = operator new(size);
		VoidPtr ptr = resource.instanceData;
		resources.Insert(name, Traits::Move(resource));
		return ptr;
	}

	VoidPtr RenderGraph::GetInstanceData(StringView name) const
	{
		if (const Resource* resource = FindResource(name))
		{
			return resource->instanceData;
		}
		return nullptr;
	}

	RenderGraphPass& RenderGraph::AddComputePass(StringView name, StringView shaderPath)
	{
		return EmplacePass(name, RenderGraphPassType::Compute);
	}

	RenderGraphPass& RenderGraph::AddComputePass(StringView name, RID shaderRID)
	{
		RenderGraphPass& pass = EmplacePass(name, RenderGraphPassType::Compute);
		pass.shader = shaderRID;
		return pass;
	}

	RenderGraphPass& RenderGraph::AddRaytracePass(StringView name, RID shaderRID)
	{
		RenderGraphPass& pass = EmplacePass(name, RenderGraphPassType::Raytrace);
		pass.shader = shaderRID;
		return pass;
	}

	RenderGraphPass& RenderGraph::AddGraphicsPass(StringView name)
	{
		return EmplacePass(name, RenderGraphPassType::Graphics);
	}

	RenderGraphPass& RenderGraph::AddPass(StringView name)
	{
		return EmplacePass(name, RenderGraphPassType::Transfer);
	}

	GPUTexture* RenderGraph::GetTexture(StringView name) const
	{
		const Resource* resource = FindResource(name);
		if (resource == nullptr) return nullptr;

		if (!resource->imported.Empty())
		{
			return resource->imported[currentOutputIndex];
		}
		if (resource->view)
		{
			return resource->view->GetTexture();
		}
		if (resource->textures[1] == nullptr)
		{
			return resource->textures[0];
		}
		return resource->textures[currentFrame];
	}

	GPUTexture* RenderGraph::GetPrevTexture(StringView name) const
	{
		const Resource* resource = FindResource(name);
		if (resource == nullptr) return nullptr;

		if (resource->textures[1] == nullptr)
		{
			return resource->textures[0];
		}
		return resource->textures[prevFrame];
	}

	GPUTextureView* RenderGraph::GetTextureView(StringView name) const
	{
		const Resource* resource = FindResource(name);
		return resource ? resource->view : nullptr;
	}

	GPUBuffer* RenderGraph::GetBuffer(StringView name) const
	{
		const Resource* resource = FindResource(name);
		if (resource == nullptr) return nullptr;
		return resource->bufferDesc.perFrame ? resource->buffers[currentFrame] : resource->buffers[0];
	}

	GPUPipeline* RenderGraph::GetOrCreatePipeline(StringView key, GPUPipeline* (*create)(VoidPtr userData), VoidPtr userData)
	{
		usize hash = HashValue(key);
		if (auto it = pipelineCache.Find(hash)) return it->second;
		GPUPipeline* pipeline = create(userData);
		pipelineCache.Insert(hash, pipeline);
		return pipeline;
	}

	GPUDescriptorSet* RenderGraph::GetDescriptorSet(RID shader, StringView variant, u32 set)
	{
		usize key = 0;
		HashCombine(key, HashValue(shader));
		HashCombine(key, HashValue(variant));
		HashCombine(key, set);
		HashCombine(key, currentFrame);
		if (auto it = descriptorSetCache.Find(key)) return it->second;
		GPUDescriptorSet* ds = device->CreateDescriptorSet(shader, variant, set);
		descriptorSetCache.Insert(key, ds);
		return ds;
	}

	GPUDescriptorSet* RenderGraph::GetSceneDescriptorSet() const
	{
		return sceneDescriptorSets[currentFrame];
	}

	GPUDescriptorSet* RenderGraph::GetSceneDescriptorSet(u32 frame) const
	{
		return sceneDescriptorSets[frame];
	}

	GPUBuffer* RenderGraph::GetSceneBuffer() const
	{
		return sceneBuffer;
	}

	u64 RenderGraph::GetSceneBufferOffset() const
	{
		return sceneBufferFrameSize * currentFrame;
	}

	u64 RenderGraph::GetSceneBufferSize() const
	{
		return sceneBufferFrameSize;
	}

	u32 RenderGraph::GetCurrentFrame() const
	{
		return currentFrame;
	}

	u32 RenderGraph::GetPrevFrame() const
	{
		return prevFrame;
	}

	u32 RenderGraph::GetTopologyBuildCount() const
	{
		return topologyBuildCount;
	}

	void RenderGraph::UpdateCamera(f32 nearClip, f32 farClip, f32 fov, Projection projection, const Mat4& view, const Vec3& cameraPosition, bool updateFrustum)
	{
	}

	void RenderGraph::SetColorOutput(StringView name)
	{
		colorOutputName = name;
	}

	void RenderGraph::SetDepthOutput(StringView name)
	{
		depthOutputName = name;
	}

	void RenderGraph::SetOutputAttachments(StringView name, Span<GPUTexture*> attachments, ResourceState requiredState)
	{
		Import(name, attachments, requiredState);
	}

	void RenderGraph::SetCurrentOutputIndex(u32 index)
	{
		currentOutputIndex = index;
	}

	void RenderGraph::SetOutputSize(Extent size)
	{
		if (outputSize.width != size.width || outputSize.height != size.height)
		{
			outputSize = size;
			resourcesDirty = true;
		}
	}

	Extent RenderGraph::GetOutputSize() const
	{
		return outputSize;
	}

	GPUTexture* RenderGraph::GetColorOutput() const
	{
		return GetTexture(colorOutputName);
	}

	GPUTexture* RenderGraph::GetDepthOutput() const
	{
		return GetTexture(depthOutputName);
	}

	Scene* RenderGraph::GetScene() const
	{
		return currentScene;
	}

	ResourceUsage RenderGraph::InferTextureUsage(StringView name) const
	{
		const Resource* resource = FindResource(name);
		if (resource == nullptr) return ResourceUsage::None;

		ResourceUsage usage = resource->textureDesc.usage;
		bool          isDepth = IsDepthFormat(resource->textureDesc.format);

		for (const RenderGraphPass* pass : passes)
		{
			for (const RenderGraphPass::Dependency& dependency : pass->dependencies)
			{
				bool referencesResource = dependency.name == name;
				if (!referencesResource)
				{
					const Resource* dependencyResource = FindResource(dependency.name);
					referencesResource = dependencyResource != nullptr
						&& dependencyResource->kind == Resource::Kind::View
						&& dependencyResource->viewDesc.texture == name;
				}
				if (!referencesResource) continue;

				switch (pass->type)
				{
					case RenderGraphPassType::Graphics:
					{
						if (dependency.access == RenderGraphAccess::Read)
						{
							usage |= ResourceUsage::ShaderResource;
						}
						else
						{
							usage |= isDepth ? ResourceUsage::DepthStencil : ResourceUsage::RenderTarget;
						}
						break;
					}
					case RenderGraphPassType::Compute:
					case RenderGraphPassType::Raytrace:
					{
						if (dependency.access == RenderGraphAccess::Read)
						{
							usage |= ResourceUsage::ShaderResource;
						}
						else if (dependency.access == RenderGraphAccess::Write)
						{
							usage |= ResourceUsage::UnorderedAccess;
						}
						else
						{
							usage |= ResourceUsage::UnorderedAccess | ResourceUsage::ShaderResource;
						}
						break;
					}
					case RenderGraphPassType::Transfer:
					{
						if (dependency.access == RenderGraphAccess::Read)
						{
							usage |= ResourceUsage::CopySource;
						}
						else if (dependency.access == RenderGraphAccess::Write)
						{
							usage |= ResourceUsage::CopyDest;
						}
						else
						{
							usage |= ResourceUsage::CopySource | ResourceUsage::CopyDest;
						}
						break;
					}
				}
			}
		}

		return usage;
	}

	ResourceUsage RenderGraph::InferBufferUsage(StringView name) const
	{
		const Resource* resource = FindResource(name);
		if (resource == nullptr) return ResourceUsage::None;

		ResourceUsage usage = resource->bufferDesc.usage;

		for (const RenderGraphPass* pass : passes)
		{
			for (const RenderGraphPass::Dependency& dependency : pass->dependencies)
			{
				if (dependency.name != name) continue;

				switch (pass->type)
				{
					case RenderGraphPassType::Transfer:
					{
						if (dependency.access == RenderGraphAccess::Read)
						{
							usage |= ResourceUsage::CopySource;
						}
						else if (dependency.access == RenderGraphAccess::Write)
						{
							usage |= ResourceUsage::CopyDest;
						}
						else
						{
							usage |= ResourceUsage::CopySource | ResourceUsage::CopyDest;
						}
						break;
					}
					case RenderGraphPassType::Graphics:
					case RenderGraphPassType::Compute:
					case RenderGraphPassType::Raytrace:
					{
						if (dependency.access == RenderGraphAccess::Read)
						{
							usage |= ResourceUsage::ShaderResource;
						}
						else if (dependency.access == RenderGraphAccess::Write)
						{
							usage |= ResourceUsage::UnorderedAccess;
						}
						else
						{
							usage |= ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess;
						}
						break;
					}
				}
			}
		}

		return usage;
	}

	void RenderGraph::CreateSceneResources()
	{
	}

	void RenderGraph::SortPasses()
	{
		if (passesSorted)
		{
			return;
		}

		if (passes.Size() < 2)
		{
			passesSorted = true;
			return;
		}

		const u32 count = static_cast<u32>(passes.Size());

		usize signature = 0;
		HashCombine(signature, count);
		for (const RenderGraphPass* pass : passes)
		{
			HashCombine(signature, HashValue(pass->name));
			HashCombine(signature, static_cast<usize>(pass->type));
			HashCombine(signature, pass->dependencies.Size());
			for (const RenderGraphPass::Dependency& dependency : pass->dependencies)
			{
				HashCombine(signature, HashValue(dependency.name));
				if (const Resource* dependencyResource = FindResource(dependency.name))
				{
					if (dependencyResource->kind == Resource::Kind::View)
					{
						HashCombine(signature, HashValue(dependencyResource->viewDesc.texture));
					}
				}
				HashCombine(signature, static_cast<usize>(dependency.access));
			}
			HashCombine(signature, pass->resolves.Size());
			for (const String& resolve : pass->resolves)
			{
				HashCombine(signature, HashValue(resolve));
			}
		}

		if (passGraphCacheValid && passGraphSignature == signature && cachedSortedPassIndices.Size() == count)
		{
			Array<RenderGraphPass*> sorted;
			sorted.Reserve(count);
			for (u32 index : cachedSortedPassIndices)
			{
				SK_ASSERT(index < count, "cached render graph pass index out of range");
				if (index >= count)
				{
					passGraphCacheValid = false;
					break;
				}
				sorted.EmplaceBack(passes[index]);
			}

			if (sorted.Size() == count)
			{
				passes = Traits::Move(sorted);
				passesSorted = true;
				return;
			}
		}

		Array<Array<u32>> edges;
		edges.Resize(count);

		Array<u32> indegrees;
		indegrees.Resize(count, 0);

		auto dependencyResourceName = [&](const RenderGraphPass::Dependency& dependency) -> String
		{
			if (const Resource* dependencyResource = FindResource(dependency.name))
			{
				if (dependencyResource->kind == Resource::Kind::View)
				{
					return dependencyResource->viewDesc.texture;
				}
			}
			return dependency.name;
		};

		HashMap<String, Array<u32>> writers;
		for (u32 passIndex = 0; passIndex < count; ++passIndex)
		{
			for (const RenderGraphPass::Dependency& dependency : passes[passIndex]->dependencies)
			{
				if (WritesResource(dependency.access))
				{
					writers[dependencyResourceName(dependency)].EmplaceBack(passIndex);
				}
			}
		}

		HashMap<String, u32>        lastWriter;
		HashMap<String, Array<u32>> readersSinceWrite;

		for (u32 passIndex = 0; passIndex < count; ++passIndex)
		{
			RenderGraphPass* pass = passes[passIndex];

			for (const RenderGraphPass::Dependency& dependency : pass->dependencies)
			{
				String resourceName = dependencyResourceName(dependency);

				if (ReadsResource(dependency.access))
				{
					bool readHasProducer = false;

					if (auto writer = lastWriter.Find(resourceName))
					{
						AddPassEdge(edges, indegrees, writer->second, passIndex);
						readHasProducer = true;
					}
					else if (auto resourceWriters = writers.Find(resourceName))
					{
						if (resourceWriters->second.Size() == 1 && resourceWriters->second[0] != passIndex)
						{
							AddPassEdge(edges, indegrees, resourceWriters->second[0], passIndex);
							readHasProducer = true;
						}
					}

					if (!readHasProducer && !WritesResource(dependency.access))
					{
						readersSinceWrite[resourceName].EmplaceBack(passIndex);
					}
				}

				if (WritesResource(dependency.access))
				{
					if (auto writer = lastWriter.Find(resourceName))
					{
						AddPassEdge(edges, indegrees, writer->second, passIndex);
					}

					if (auto readers = readersSinceWrite.Find(resourceName))
					{
						for (u32 reader : readers->second)
						{
							AddPassEdge(edges, indegrees, reader, passIndex);
						}
						readers->second.Clear();
					}

					if (auto writer = lastWriter.Find(resourceName))
					{
						writer->second = passIndex;
					}
					else
					{
						lastWriter.Insert(resourceName, passIndex);
					}
				}
			}
		}

		Array<u32> sortedIndices;
		sortedIndices.Reserve(count);

		Array<bool> emitted;
		emitted.Resize(count, false);

		while (sortedIndices.Size() < count)
		{
			u32 next = U32_MAX;
			for (u32 passIndex = 0; passIndex < count; ++passIndex)
			{
				if (!emitted[passIndex] && indegrees[passIndex] == 0)
				{
					next = passIndex;
					break;
				}
			}

			if (next == U32_MAX)
			{
				SK_ASSERT(false, "RenderGraph contains cyclic pass dependencies");
				return;
			}

			emitted[next] = true;
			sortedIndices.EmplaceBack(next);

			for (u32 dependent : edges[next])
			{
				--indegrees[dependent];
			}
		}

		Array<RenderGraphPass*> sorted;
		sorted.Reserve(count);
		for (u32 index : sortedIndices)
		{
			sorted.EmplaceBack(passes[index]);
		}

		cachedSortedPassIndices = sortedIndices;
		passGraphSignature = signature;
		passGraphCacheValid = true;
		++topologyBuildCount;

		passes = Traits::Move(sorted);
		passesSorted = true;
	}

	void RenderGraph::CreateResourceTextures()
	{
		for (auto& it : resources)
		{
			const String& name = it.first;
			Resource&     res = it.second;

			if (res.kind == Resource::Kind::Texture)
			{
				if (res.textures[0] != nullptr) continue;

				const RenderGraphTextureDesc& rgDesc = res.textureDesc;
				bool                          followOutput = rgDesc.extent.width == 0 && rgDesc.extent.height == 0;
				Extent                        size = followOutput
					                                     ? Extent{static_cast<u32>(outputSize.width * rgDesc.scale.x), static_cast<u32>(outputSize.height * rgDesc.scale.y)}
					                                     : rgDesc.extent;

				TextureDesc desc;
				desc.extent = Extent3D{size.width, size.height, 1};
				desc.format = rgDesc.format;
				desc.mipLevels = rgDesc.mipLevels;
				desc.arrayLayers = rgDesc.arrayLayers;
				desc.sampleCount = rgDesc.samples;
				desc.cubemap = rgDesc.cubemap;
				desc.usage = InferTextureUsage(name);
				desc.debugName = name;

				res.textures[0] = device->CreateTexture(desc);
				res.states[0] = ResourceState::Undefined;
				res.textureStates[0].Resize(TextureMipCount(res.textures[0]) * TextureLayerCount(res.textures[0]), ResourceState::Undefined);
				res.textureScopes[0].Resize(res.textureStates[0].Size(), BarrierSyncScope::Automatic);
				res.textureLastWrites[0].Resize(res.textureStates[0].Size(), false);

				if (rgDesc.pingPong)
				{
					res.textures[1] = device->CreateTexture(desc);
					res.states[1] = ResourceState::Undefined;
					res.textureStates[1].Resize(TextureMipCount(res.textures[1]) * TextureLayerCount(res.textures[1]), ResourceState::Undefined);
					res.textureScopes[1].Resize(res.textureStates[1].Size(), BarrierSyncScope::Automatic);
					res.textureLastWrites[1].Resize(res.textureStates[1].Size(), false);
				}
			}
			else if (res.kind == Resource::Kind::Buffer)
			{
				if (res.buffers[0] != nullptr) continue;

				const RenderGraphBufferDesc& rgDesc = res.bufferDesc;

				BufferDesc desc;
				desc.size = rgDesc.size;
				desc.usage = InferBufferUsage(name);
				desc.hostVisible = rgDesc.hostVisible;
				desc.persistentMapped = rgDesc.persistentMapped;
				desc.debugName = name;

				u32 count = rgDesc.perFrame ? SK_FRAMES_IN_FLIGHT : 1;
				for (u32 i = 0; i < count; ++i)
				{
					res.buffers[i] = device->CreateBuffer(desc);
					res.bufferStates[i] = ResourceState::Undefined;
					res.bufferScopes[i] = BarrierSyncScope::Automatic;
					res.bufferLastWrites[i] = false;
				}
			}
		}

		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (res.kind != Resource::Kind::View || res.view != nullptr) continue;

			Resource* source = FindResource(res.viewDesc.texture);
			if (source == nullptr || source->textures[0] == nullptr) continue;

			TextureViewDesc desc;
			desc.texture = source->textures[0];
			desc.type = res.viewDesc.viewType;
			desc.baseMipLevel = res.viewDesc.baseMipLevel;
			desc.mipLevelCount = res.viewDesc.mipLevelCount;
			desc.baseArrayLayer = res.viewDesc.baseArrayLayer;
			desc.arrayLayerCount = res.viewDesc.arrayLayerCount;
			desc.debugName = it.first;
			res.view = device->CreateTextureView(desc);
		}
	}

	void RenderGraph::CreateRenderPasses()
	{
	}

	void RenderGraph::CreateBarriers()
	{
	}

	void RenderGraph::UpdateSceneBuffer()
	{
	}

	void RenderGraph::Begin(Scene* scene)
	{
		currentScene = scene;
		prevFrame = currentFrame;
		currentFrame = (currentFrame + 1) % SK_FRAMES_IN_FLIGHT;

		for (RenderGraphPass* pass : passes)
		{
			passPool.EmplaceBack(pass);
		}
		passes.Clear();
		passesSorted = false;
	}

	void RenderGraph::Resize(Extent newExtent)
	{
		SetOutputSize(newExtent);
	}

	void RenderGraph::Execute(GPUCommandBuffer* cmd)
	{
		SortPasses();

		if (resourcesDirty)
		{
			CreateResourceTextures();
			resourcesDirty = false;
		}

		struct TextureBarrierTarget
		{
			GPUTexture*           texture = nullptr;
			Array<ResourceState>* states = nullptr;
			Array<BarrierSyncScope>* scopes = nullptr;
			Array<bool>*          lastWrites = nullptr;
			ResourceState*        coarseState = nullptr;
			u32                   baseMipLevel = 0;
			u32                   mipLevelCount = 0;
			u32                   baseArrayLayer = 0;
			u32                   arrayLayerCount = 0;
		};

		auto ensureTextureStateTracking = [](GPUTexture* texture, Array<ResourceState>& states, Array<BarrierSyncScope>& scopes, Array<bool>& lastWrites, ResourceState initialState)
		{
			const u32 subresourceCount = TextureMipCount(texture) * TextureLayerCount(texture);
			if (states.Size() != subresourceCount)
			{
				states.Resize(subresourceCount, initialState);
			}
			if (scopes.Size() != subresourceCount)
			{
				scopes.Resize(subresourceCount, BarrierSyncScope::Automatic);
			}
			if (lastWrites.Size() != subresourceCount)
			{
				lastWrites.Resize(subresourceCount, false);
			}
		};

		auto resolveWholeTextureResource = [&](Resource& res, TextureBarrierTarget& target) -> bool
		{
			if (res.kind == Resource::Kind::Texture)
			{
				const u32 slot = res.textures[1] != nullptr ? currentFrame : 0;
				GPUTexture* texture = res.textures[slot];
				if (texture == nullptr) return false;

				ensureTextureStateTracking(texture, res.textureStates[slot], res.textureScopes[slot], res.textureLastWrites[slot], res.states[slot]);

				target.texture = texture;
				target.states = &res.textureStates[slot];
				target.scopes = &res.textureScopes[slot];
				target.lastWrites = &res.textureLastWrites[slot];
				target.coarseState = &res.states[slot];
				target.baseMipLevel = 0;
				target.mipLevelCount = TextureMipCount(texture);
				target.baseArrayLayer = 0;
				target.arrayLayerCount = TextureLayerCount(texture);
				return true;
			}

			if (res.kind == Resource::Kind::Imported)
			{
				if (res.imported.Empty() || currentOutputIndex >= res.imported.Size()) return false;
				GPUTexture* texture = res.imported[currentOutputIndex];
				if (texture == nullptr) return false;
				if (currentOutputIndex >= res.importedStates.Size() || currentOutputIndex >= res.importedScopes.Size() || currentOutputIndex >= res.importedLastWrites.Size()) return false;

				ensureTextureStateTracking(texture, res.importedStates[currentOutputIndex], res.importedScopes[currentOutputIndex], res.importedLastWrites[currentOutputIndex], res.importedState);

				target.texture = texture;
				target.states = &res.importedStates[currentOutputIndex];
				target.scopes = &res.importedScopes[currentOutputIndex];
				target.lastWrites = &res.importedLastWrites[currentOutputIndex];
				target.coarseState = nullptr;
				target.baseMipLevel = 0;
				target.mipLevelCount = TextureMipCount(texture);
				target.baseArrayLayer = 0;
				target.arrayLayerCount = TextureLayerCount(texture);
				return true;
			}

			return false;
		};

		auto resolveTextureDependency = [&](Resource& res, TextureBarrierTarget& target) -> bool
		{
			if (res.kind != Resource::Kind::View)
			{
				return resolveWholeTextureResource(res, target);
			}

			Resource* source = FindResource(res.viewDesc.texture);
			if (source == nullptr || !resolveWholeTextureResource(*source, target)) return false;

			const u32 totalMipLevels = TextureMipCount(target.texture);
			const u32 totalArrayLayers = TextureLayerCount(target.texture);
			if (res.viewDesc.baseMipLevel >= totalMipLevels || res.viewDesc.baseArrayLayer >= totalArrayLayers) return false;

			const u32 availableMipLevels = totalMipLevels - res.viewDesc.baseMipLevel;
			const u32 availableArrayLayers = totalArrayLayers - res.viewDesc.baseArrayLayer;

			target.baseMipLevel = res.viewDesc.baseMipLevel;
			target.mipLevelCount = res.viewDesc.mipLevelCount == U32_MAX || res.viewDesc.mipLevelCount > availableMipLevels
				                       ? availableMipLevels
				                       : res.viewDesc.mipLevelCount;
			target.baseArrayLayer = res.viewDesc.baseArrayLayer;
			target.arrayLayerCount = res.viewDesc.arrayLayerCount == U32_MAX || res.viewDesc.arrayLayerCount > availableArrayLayers
				                         ? availableArrayLayers
				                         : res.viewDesc.arrayLayerCount;

			return target.mipLevelCount != 0 && target.arrayLayerCount != 0;
		};

		auto transitionTexture = [](GPUCommandBuffer* cmd, TextureBarrierTarget& resource, ResourceState targetState, BarrierSyncScope targetScope, bool writes)
		{
			const u32 arrayLayers = TextureLayerCount(resource.texture);

			bool          first = true;
			bool          canBatch = true;
			bool          batchedNeedsBarrier = false;
			ResourceState batchedOldState = ResourceState::Undefined;
			BarrierSyncScope batchedOldScope = BarrierSyncScope::Automatic;

			for (u32 mip = resource.baseMipLevel; mip < resource.baseMipLevel + resource.mipLevelCount; ++mip)
			{
				for (u32 layer = resource.baseArrayLayer; layer < resource.baseArrayLayer + resource.arrayLayerCount; ++layer)
				{
					const u32 index = SubresourceIndex(mip, layer, arrayLayers);
					const ResourceState oldState = (*resource.states)[index];
					const BarrierSyncScope oldScope = (*resource.scopes)[index];
					const bool needsBarrier = oldState != targetState || (*resource.lastWrites)[index] || writes;

					if (first)
					{
						batchedOldState = oldState;
						batchedOldScope = oldScope;
						batchedNeedsBarrier = needsBarrier;
						first = false;
					}
					else if (oldState != batchedOldState || oldScope != batchedOldScope || needsBarrier != batchedNeedsBarrier)
					{
						canBatch = false;
					}
				}
			}

			if (canBatch)
			{
				if (batchedNeedsBarrier)
				{
					cmd->ResourceBarrier(TextureBarrierDesc{
						.texture = resource.texture,
						.oldState = batchedOldState,
						.newState = targetState,
						.srcScope = batchedOldScope,
						.dstScope = targetScope,
						.baseMipLevel = resource.baseMipLevel,
						.mipLevelCount = resource.mipLevelCount,
						.baseArrayLayer = resource.baseArrayLayer,
						.arrayLayerCount = resource.arrayLayerCount
					});
				}

				for (u32 mip = resource.baseMipLevel; mip < resource.baseMipLevel + resource.mipLevelCount; ++mip)
				{
					for (u32 layer = resource.baseArrayLayer; layer < resource.baseArrayLayer + resource.arrayLayerCount; ++layer)
					{
						const u32 index = SubresourceIndex(mip, layer, arrayLayers);
						(*resource.states)[index] = targetState;
						(*resource.scopes)[index] = targetScope;
						(*resource.lastWrites)[index] = writes;
					}
				}
			}
			else
			{
				for (u32 mip = resource.baseMipLevel; mip < resource.baseMipLevel + resource.mipLevelCount; ++mip)
				{
					for (u32 layer = resource.baseArrayLayer; layer < resource.baseArrayLayer + resource.arrayLayerCount; ++layer)
					{
						const u32 index = SubresourceIndex(mip, layer, arrayLayers);
						const ResourceState oldState = (*resource.states)[index];
						const BarrierSyncScope oldScope = (*resource.scopes)[index];
						if (oldState != targetState || (*resource.lastWrites)[index] || writes)
						{
							cmd->ResourceBarrier(TextureBarrierDesc{
								.texture = resource.texture,
								.oldState = oldState,
								.newState = targetState,
								.srcScope = oldScope,
								.dstScope = targetScope,
								.baseMipLevel = mip,
								.mipLevelCount = 1,
								.baseArrayLayer = layer,
								.arrayLayerCount = 1
							});
						}
						(*resource.states)[index] = targetState;
						(*resource.scopes)[index] = targetScope;
						(*resource.lastWrites)[index] = writes;
					}
				}
			}

			if (resource.coarseState != nullptr)
			{
				*resource.coarseState = targetState;
			}
		};

		auto transitionBuffer = [](GPUCommandBuffer* cmd, GPUBuffer* buffer, ResourceState& currentState, BarrierSyncScope& currentScope, bool& lastWrite, ResourceState targetState, BarrierSyncScope targetScope, bool writes)
		{
			if (currentState != targetState || lastWrite || writes)
			{
				cmd->ResourceBarrier(BufferBarrierDesc{
					.buffer = buffer,
					.oldState = currentState,
					.newState = targetState,
					.srcScope = currentScope,
					.dstScope = targetScope
				});
			}
			currentState = targetState;
			currentScope = targetScope;
			lastWrite = writes;
		};

		for (RenderGraphPass* pass : passes)
		{
			const BarrierSyncScope passScope = PassSyncScope(pass->type);

			for (const RenderGraphPass::Dependency& dependency : pass->dependencies)
			{
				Resource* res = FindResource(dependency.name);
				if (res == nullptr) continue;

				if (res->kind == Resource::Kind::Buffer)
				{
					const u32 slot = res->bufferDesc.perFrame ? currentFrame : 0;
					GPUBuffer* buffer = res->buffers[slot];
					if (buffer == nullptr) continue;

					const ResourceState target = PassBufferTargetState(pass->type, dependency.access);
					transitionBuffer(cmd, buffer, res->bufferStates[slot], res->bufferScopes[slot], res->bufferLastWrites[slot], target, passScope, WritesResource(dependency.access));
					continue;
				}

				if (res->kind == Resource::Kind::Instance)
				{
					continue;
				}

				TextureBarrierTarget textureTarget;
				if (!resolveTextureDependency(*res, textureTarget)) continue;

				const ResourceState target = PassTargetState(pass->type, dependency.access, IsDepthFormat(textureTarget.texture->GetDesc().format));
				transitionTexture(cmd, textureTarget, target, passScope, WritesResource(dependency.access));
			}

			if (pass->renderFn)
			{
				pass->renderFn(*this, currentScene, cmd);
			}
		}

		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (res.kind != Resource::Kind::Imported || res.importedState == ResourceState::Undefined) continue;

			TextureBarrierTarget textureTarget;
			if (resolveWholeTextureResource(res, textureTarget))
			{
				transitionTexture(cmd, textureTarget, res.importedState, BarrierSyncScope::Automatic, false);
			}
		}
	}
}
