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
					return access == RenderGraphAccess::Read ? ResourceState::CopySource : ResourceState::CopyDest;
			}
			return ResourceState::General;
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
				if (dependency.name != name) continue;

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
						usage |= dependency.access == RenderGraphAccess::Read ? ResourceUsage::CopySource : ResourceUsage::CopyDest;
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

				if (rgDesc.pingPong)
				{
					res.textures[1] = device->CreateTexture(desc);
					res.states[1] = ResourceState::Undefined;
				}
			}
			else if (res.kind == Resource::Kind::Buffer)
			{
				if (res.buffers[0] != nullptr) continue;

				const RenderGraphBufferDesc& rgDesc = res.bufferDesc;

				BufferDesc desc;
				desc.size = rgDesc.size;
				desc.usage = rgDesc.usage;
				desc.hostVisible = rgDesc.hostVisible;
				desc.persistentMapped = rgDesc.persistentMapped;
				desc.debugName = name;

				u32 count = rgDesc.perFrame ? SK_FRAMES_IN_FLIGHT : 1;
				for (u32 i = 0; i < count; ++i)
				{
					res.buffers[i] = device->CreateBuffer(desc);
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
	}

	void RenderGraph::Resize(Extent newExtent)
	{
		SetOutputSize(newExtent);
	}

	void RenderGraph::Execute(GPUCommandBuffer* cmd)
	{
		if (resourcesDirty)
		{
			CreateResourceTextures();
			resourcesDirty = false;
		}

		for (RenderGraphPass* pass : passes)
		{
			for (const RenderGraphPass::Dependency& dependency : pass->dependencies)
			{
				Resource* res = FindResource(dependency.name);
				if (res == nullptr) continue;

				u32         slot = res->textures[1] != nullptr ? currentFrame : 0;
				GPUTexture* texture = res->textures[slot];
				if (texture == nullptr && !res->imported.Empty())
				{
					texture = res->imported[currentOutputIndex];
				}
				if (texture == nullptr) continue;

				ResourceState target = PassTargetState(pass->type, dependency.access, IsDepthFormat(res->textureDesc.format));
				if (res->states[slot] != target)
				{
					cmd->ResourceBarrier(texture, res->states[slot], target, 0, U32_MAX, 0, U32_MAX);
					res->states[slot] = target;
				}
			}

			if (pass->renderFn)
			{
				pass->renderFn(*this, currentScene, cmd);
			}
		}
	}
}
