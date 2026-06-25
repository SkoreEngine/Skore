#include "RenderGraph.hpp"

#include "Graphics.hpp"
#include "RenderSceneObjects.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Algorithm.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	struct GlobalSceneBuffer
	{
		Mat4  viewProjection;
		Mat4  view;
		Mat4  projection;
		Mat4  viewInv;
		Mat4  projectionInv;
		Mat4  viewProjInv;

		Vec3  cameraPosition;
		f32   farClip;

		IVec2 outputSize;
		Vec2  pad1;

		Vec2  jitter;
		Vec2  prevJitter;

		u32   instanceCount;
		u32   pad0;
		u64   cullingMask;

		Vec4  frustumPlanes[6];
	};

	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::RenderGraph");

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

		Extent3D DispatchGroupCount(GPUPipeline* pipeline, u32 x, u32 y, u32 z)
		{
			const Extent3D numThreads = pipeline->GetPipelineDesc().numThreads;
			const u32      threadsX = numThreads.width != 0 ? numThreads.width : 1;
			const u32      threadsY = numThreads.height != 0 ? numThreads.height : 1;
			const u32      threadsZ = numThreads.depth != 0 ? numThreads.depth : 1;
			const u32      sizeX = x != 0 ? x : 1;
			const u32      sizeY = y != 0 ? y : 1;
			const u32      sizeZ = z != 0 ? z : 1;
			return Extent3D{(sizeX + threadsX - 1) / threadsX, (sizeY + threadsY - 1) / threadsY, (sizeZ + threadsZ - 1) / threadsZ};
		}
	}

	RenderGraph::RenderGraph(GPUDevice* device) : device(device) {}

	GPUDevice* RenderGraph::GetDevice() const
	{
		return device;
	}

	RenderGraph::~RenderGraph()
	{
		device->WaitIdle();

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

		for (auto& it : framebufferCache)
		{
			it.second->Destroy();
		}
		framebufferCache.Clear();

		for (auto& it : renderPassCache)
		{
			it.second->Destroy();
		}
		renderPassCache.Clear();

		for (GPUPipeline* pipeline : ownedPipelines)
		{
			pipeline->Destroy();
		}
		ownedPipelines.Clear();
		pipelineCache.Clear();

		for (auto& it : descriptorSetCache)
		{
			it.second->Destroy();
		}
		descriptorSetCache.Clear();

		for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			if (sceneDescriptorSets[i] != nullptr)
			{
				sceneDescriptorSets[i]->Destroy();
				sceneDescriptorSets[i] = nullptr;
			}
		}

		if (sceneBuffer != nullptr)
		{
			sceneBuffer->Destroy();
			sceneBuffer = nullptr;
		}

		for (auto& it : resources)
		{
			Resource& res = it.second;

			for (GPUTexture*& texture : res.textures)
			{
				if (texture != nullptr)
				{
					texture->Destroy();
					texture = nullptr;
				}
			}

			if (res.view != nullptr)
			{
				res.view->Destroy();
				res.view = nullptr;
			}

			for (GPUBuffer*& buffer : res.buffers)
			{
				if (buffer != nullptr)
				{
					buffer->Destroy();
					buffer = nullptr;
				}
			}

			if (res.instanceData != nullptr)
			{
				operator delete(res.instanceData);
				res.instanceData = nullptr;
			}
		}

		for (GPUMemory* heap : aliasHeaps)
		{
			if (heap != nullptr) heap->Destroy();
		}
		aliasHeaps.Clear();
	}

	RenderGraphPass& RenderGraphPass::Write(StringView name)
	{
		AddDependency(name, RenderGraphAccess::Write);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Read(StringView name)
	{
		AddDependency(name, RenderGraphAccess::Read);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::WriteRead(StringView name)
	{
		AddDependency(name, RenderGraphAccess::ReadWrite);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::Resolve(StringView name)
	{
		AddResolve(name);
		return *this;
	}

	RenderGraphPass& RenderGraphPass::DescriptorSet(u32 set, GPUDescriptorSet* ds)
	{
		for (usize i = 0; i < boundDescriptorSetCount; ++i)
		{
			if (boundDescriptorSets[i].set == set)
			{
				return *this;
			}
		}

		BoundDescriptorSet* binding = nullptr;
		if (boundDescriptorSetCount < boundDescriptorSets.Size())
		{
			binding = &boundDescriptorSets[boundDescriptorSetCount];
		}
		else
		{
			binding = &boundDescriptorSets.EmplaceBack();
		}

		binding->set = set;
		binding->descriptorSet = ds;
		++boundDescriptorSetCount;
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
		framebuffer = nullptr;

		dependencyCount = 0;
		resolveCount = 0;
		boundDescriptorSetCount = 0;

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

	void RenderGraphPass::AddDependency(StringView name, RenderGraphAccess access)
	{
		Dependency* dependency = nullptr;
		if (dependencyCount < dependencies.Size())
		{
			dependency = &dependencies[dependencyCount];
		}
		else
		{
			dependency = &dependencies.EmplaceBack();
		}

		SetNameReference(dependency->nameStorage, dependency->name, name);
		dependency->access = access;
		++dependencyCount;
	}

	void RenderGraphPass::AddResolve(StringView name)
	{
		ResolveTarget* resolve = nullptr;
		if (resolveCount < resolves.Size())
		{
			resolve = &resolves[resolveCount];
		}
		else
		{
			resolve = &resolves.EmplaceBack();
		}

		SetNameReference(resolve->nameStorage, resolve->name, name);
		++resolveCount;
	}

	void RenderGraphPass::SetNameReference(String& storage, StringView& view, StringView name)
	{
		if (graph != nullptr)
		{
			StringView resourceName = graph->FindResourceName(name);
			if (!resourceName.Empty())
			{
				view = resourceName;
				return;
			}
		}

		storage = name;
		view = storage;
	}

	bool RenderGraphPass::HasResolve(StringView name) const
	{
		for (usize i = 0; i < resolveCount; ++i)
		{
			if (resolves[i].name == name)
			{
				return true;
			}
		}
		return false;
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

	StringView RenderGraph::FindResourceName(StringView name) const
	{
		auto it = resources.Find(name);
		return it != resources.end() ? StringView(it->first) : StringView{};
	}

	void RenderGraph::Create(StringView name, const RenderGraphTextureDesc& textureDesc)
	{
		if (Resource* existing = FindResource(name))
		{
			existing->textureDesc = textureDesc;
			existing->lastUsed = frameGeneration;
			return;
		}

		Resource resource;
		resource.kind = Resource::Kind::Texture;
		resource.textureDesc = textureDesc;
		resource.lastUsed = frameGeneration;
		resources.Insert(name, Traits::Move(resource));
		resourcesDirty = true;
	}

	void RenderGraph::Create(StringView name, const RenderGraphBufferDesc& bufferDesc)
	{
		if (Resource* existing = FindResource(name))
		{
			existing->bufferDesc = bufferDesc;
			existing->lastUsed = frameGeneration;
			return;
		}

		Resource resource;
		resource.kind = Resource::Kind::Buffer;
		resource.bufferDesc = bufferDesc;
		resource.lastUsed = frameGeneration;
		resources.Insert(name, Traits::Move(resource));
		resourcesDirty = true;
	}

	void RenderGraph::CreateView(StringView name, const RenderGraphViewDesc& viewDesc)
	{
		if (Resource* existing = FindResource(name))
		{
			existing->viewDesc = viewDesc;
			existing->lastUsed = frameGeneration;
			return;
		}

		Resource resource;
		resource.kind = Resource::Kind::View;
		resource.viewDesc = viewDesc;
		resource.lastUsed = frameGeneration;
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
		resource->lastUsed = frameGeneration;
		resource->imported.Resize(textures.Size());
		for (usize i = 0; i < textures.Size(); ++i)
		{
			resource->imported[i] = textures[i];
		}
		resource->importedState = state;
		resource->importedStates.Resize(textures.Size());
		resource->importedScopes.Resize(textures.Size());
		resource->importedLastWrites.Resize(textures.Size());

		for (usize i = 0; i < resource->imported.Size(); ++i)
		{
			GPUTexture* texture = resource->imported[i];
			Array<ResourceState>& states = resource->importedStates[i];
			Array<BarrierSyncScope>& scopes = resource->importedScopes[i];
			Array<bool>& lastWrites = resource->importedLastWrites[i];

			usize subresourceCount = 0;
			if (texture != nullptr)
			{
				subresourceCount = TextureMipCount(texture) * TextureLayerCount(texture);
			}

			states.Resize(subresourceCount);
			scopes.Resize(subresourceCount);
			lastWrites.Resize(subresourceCount);

			for (usize subresource = 0; subresource < subresourceCount; ++subresource)
			{
				states[subresource] = state;
				scopes[subresource] = BarrierSyncScope::Automatic;
				lastWrites[subresource] = false;
			}
		}
	}

	VoidPtr RenderGraph::CreateInstance(StringView name, usize size)
	{
		if (Resource* existing = FindResource(name))
		{
			if (existing->instanceData && existing->instanceSize == size)
			{
				existing->lastUsed = frameGeneration;
				return existing->instanceData;
			}
		}

		Resource resource;
		resource.kind = Resource::Kind::Instance;
		resource.instanceSize = size;
		resource.instanceData = operator new(size);
		resource.lastUsed = frameGeneration;
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
		RenderGraphPass& pass = EmplacePass(name, RenderGraphPassType::Compute);
		pass.shader = Resources::FindByPath(shaderPath);
		return pass;
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
		bool applyJitter = false;
		for (const RenderGraphPass* pass : passes)
		{
			if (pass->requireJitter)
			{
				applyJitter = true;
				break;
			}
		}

		camera.view = view;
		camera.invView = Mat4::Inverse(camera.view);
		camera.projection = Mat4::Perspective(Math::Radians(fov), outputSize.width / static_cast<f32>(outputSize.height), nearClip, farClip);
		camera.invProjection = Mat4::Inverse(camera.projection);

		camera.nearClip = nearClip;
		camera.farClip = farClip;

		camera.cameraPosition = cameraPosition;
		camera.previousViewProjection = camera.viewProjection;
		camera.previousViewProjectionNoJitter = camera.viewProjectionNoJitter;
		camera.projectionNoJitter = camera.projection;
		camera.viewProjectionNoJitter = camera.projection * camera.view;

		if (applyJitter)
		{
			Vec2 jitterValues = Halton23Sequence(camera.jitterIndex);
			camera.jitterIndex = (camera.jitterIndex + 1) % camera.jitterPeriod;

			Vec2 jitterOffsets = Vec2{jitterValues.x * 2.0f - 1.0f, jitterValues.y * 2.0f - 1.0f};

			camera.previousJitter = camera.jitter;
			camera.jitter = Vec2{jitterOffsets.x / static_cast<f32>(outputSize.width), jitterOffsets.y / static_cast<f32>(outputSize.height)};

			Mat4 jitterMatrix = Mat4::Translate(Mat4{1.0}, Vec3{camera.jitter.x, camera.jitter.y, 0.0f});
			camera.projection = jitterMatrix * camera.projection;
		}

		camera.viewProjection = camera.projection * camera.view;
		camera.invViewProj = Mat4::Inverse(camera.viewProjection);

		if (updateFrustum)
		{
			camera.frustum = Math::CreateFrustumFromCamera(camera.viewProjection);
		}
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
			sizeChanged = true;
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
			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
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
			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
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
		if (sceneBuffer != nullptr) return;

		u64 alignment = device->GetProperties().limits.minMemoryMapAlignment;
		if (alignment == 0) alignment = 1;
		sceneBufferFrameSize = AlignedSize(static_cast<u64>(sizeof(GlobalSceneBuffer)), alignment);

		BufferDesc bufferDesc;
		bufferDesc.size = sceneBufferFrameSize * SK_FRAMES_IN_FLIGHT;
		bufferDesc.usage = ResourceUsage::ConstantBuffer;
		bufferDesc.hostVisible = true;
		bufferDesc.persistentMapped = true;
		bufferDesc.debugName = "RenderGraph_sceneBuffer";
		sceneBuffer = device->CreateBuffer(bufferDesc);

		DescriptorSetDesc desc;
		desc.bindings = {
			DescriptorSetLayoutBinding{.binding = 0, .descriptorType = DescriptorType::UniformBuffer},
			DescriptorSetLayoutBinding{.binding = 1, .descriptorType = DescriptorType::StorageBuffer},
			DescriptorSetLayoutBinding{.binding = 2, .descriptorType = DescriptorType::UniformBuffer},
			DescriptorSetLayoutBinding{.binding = 3, .descriptorType = DescriptorType::SampledImage},
			DescriptorSetLayoutBinding{.binding = 4, .descriptorType = DescriptorType::Sampler}
		};
		if (device->GetFeatures().rayTracing)
		{
			desc.bindings.EmplaceBack(DescriptorSetLayoutBinding{.binding = 6, .descriptorType = DescriptorType::AccelerationStructure});
		}
		desc.debugName = "RenderGraph_sceneDescriptorSet";

		for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			sceneDescriptorSets[i] = device->CreateDescriptorSet(desc);

			DescriptorUpdate descriptorUpdate;
			descriptorUpdate.type = DescriptorType::UniformBuffer;
			descriptorUpdate.binding = 0;
			descriptorUpdate.buffer = sceneBuffer;
			descriptorUpdate.bufferOffset = sceneBufferFrameSize * i;
			descriptorUpdate.bufferRange = sizeof(GlobalSceneBuffer);
			sceneDescriptorSets[i]->Update(descriptorUpdate);
		}
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
			HashCombine(signature, pass->dependencyCount);
			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
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
			HashCombine(signature, pass->resolveCount);
			for (usize resolveIndex = 0; resolveIndex < pass->resolveCount; ++resolveIndex)
			{
				HashCombine(signature, HashValue(pass->resolves[resolveIndex].name));
			}
		}

		if (passGraphCacheValid && passGraphSignature == signature && cachedSortedPassIndices.Size() == count)
		{
			sortedPassScratch.Clear();
			sortedPassScratch.Reserve(count);
			for (u32 index : cachedSortedPassIndices)
			{
				SK_ASSERT(index < count, "cached render graph pass index out of range");
				if (index >= count)
				{
					passGraphCacheValid = false;
					break;
				}
				sortedPassScratch.EmplaceBack(passes[index]);
			}

			if (sortedPassScratch.Size() == count)
			{
				for (u32 i = 0; i < count; ++i)
				{
					passes[i] = sortedPassScratch[i];
				}
				passesSorted = true;
				return;
			}
		}

		Array<Array<u32>> edges;
		edges.Resize(count);

		Array<u32> indegrees;
		indegrees.Resize(count, 0);

		auto dependencyResourceName = [&](const RenderGraphPass::Dependency& dependency) -> StringView
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

		HashMap<StringView, Array<u32>> writers;
		for (u32 passIndex = 0; passIndex < count; ++passIndex)
		{
			RenderGraphPass* pass = passes[passIndex];
			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
				if (WritesResource(dependency.access))
				{
					writers[dependencyResourceName(dependency)].EmplaceBack(passIndex);
				}
			}
		}

		HashMap<StringView, u32>        lastWriter;
		HashMap<StringView, Array<u32>> readersSinceWrite;

		for (u32 passIndex = 0; passIndex < count; ++passIndex)
		{
			RenderGraphPass* pass = passes[passIndex];

			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
				StringView                         resourceName = dependencyResourceName(dependency);

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

		sortedPassScratch.Clear();
		sortedPassScratch.Reserve(count);
		for (u32 index : sortedIndices)
		{
			sortedPassScratch.EmplaceBack(passes[index]);
		}

		cachedSortedPassIndices = sortedIndices;
		passGraphSignature = signature;
		passGraphCacheValid = true;
		++topologyBuildCount;

		for (u32 i = 0; i < count; ++i)
		{
			passes[i] = sortedPassScratch[i];
		}
		passesSorted = true;
	}

	TextureDesc RenderGraph::BuildTextureDesc(StringView name, const Resource& res) const
	{
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
		return desc;
	}

	void RenderGraph::CreateResourceTextures()
	{
		BuildAliasGroup();

		for (auto& it : resources)
		{
			const String& name = it.first;
			Resource&     res = it.second;

			if (res.kind == Resource::Kind::Texture)
			{
				if (res.textures[0] != nullptr) continue;

				const RenderGraphTextureDesc& rgDesc = res.textureDesc;
				TextureDesc                   desc = BuildTextureDesc(name, res);

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

	RenderGraphAliasPlan ComputeRenderGraphAliasPlan(const Array<RenderGraphAliasResource>& resources)
	{
		auto alignUp = [](u64 value, u64 alignment) -> u64
		{
			if (alignment == 0) return value;
			return ((value + alignment - 1) / alignment) * alignment;
		};

		RenderGraphAliasPlan plan;
		plan.placements.Resize(resources.Size());

		Array<u32> order;
		for (u32 i = 0; i < resources.Size(); ++i)
		{
			order.EmplaceBack(i);
		}

		Sort(order.Data(), order.Data() + order.Size(), [&](u32 a, u32 b)
		{
			const RenderGraphAliasResource& ra = resources[a];
			const RenderGraphAliasResource& rb = resources[b];
			if (ra.size != rb.size) return ra.size > rb.size;
			if (ra.firstPass != rb.firstPass) return ra.firstPass < rb.firstPass;
			if (ra.lastPass != rb.lastPass) return ra.lastPass < rb.lastPass;
			return a < b;
		});

		struct Placed
		{
			u32 firstPass;
			u32 lastPass;
			u64 offset;
			u64 size;
		};
		Array<Array<Placed>> heapTenants;

		for (u32 idx : order)
		{
			const RenderGraphAliasResource& r = resources[idx];

			u32 chosenHeap = U32_MAX;
			u64 chosenOffset = 0;

			for (u32 h = 0; h < plan.buckets.Size(); ++h)
			{
				if ((plan.buckets[h].memoryTypeBits & r.memoryTypeBits) == 0) continue;

				Array<Placed> blocked;
				for (const Placed& tenant : heapTenants[h])
				{
					if (r.firstPass <= tenant.lastPass && tenant.firstPass <= r.lastPass)
					{
						blocked.EmplaceBack(tenant);
					}
				}

				Sort(blocked.Data(), blocked.Data() + blocked.Size(), [](const Placed& a, const Placed& b)
				{
					return a.offset < b.offset;
				});

				u64 candidate = 0;
				for (const Placed& block : blocked)
				{
					candidate = alignUp(candidate, r.alignment);
					if (candidate + r.size <= block.offset) break;
					candidate = Math::Max(candidate, block.offset + block.size);
				}

				chosenOffset = alignUp(candidate, r.alignment);
				chosenHeap = h;
				break;
			}

			if (chosenHeap == U32_MAX)
			{
				chosenHeap = static_cast<u32>(plan.buckets.Size());
				chosenOffset = 0;

				RenderGraphAliasBucket bucket;
				bucket.size = 0;
				bucket.alignment = r.alignment;
				bucket.memoryTypeBits = r.memoryTypeBits;
				plan.buckets.EmplaceBack(bucket);
				heapTenants.EmplaceBack();
			}

			RenderGraphAliasBucket& bucket = plan.buckets[chosenHeap];
			bucket.size = Math::Max(bucket.size, chosenOffset + r.size);
			bucket.alignment = Math::Max(bucket.alignment, r.alignment);
			bucket.memoryTypeBits &= r.memoryTypeBits;

			heapTenants[chosenHeap].EmplaceBack(Placed{r.firstPass, r.lastPass, chosenOffset, r.size});

			plan.placements[idx].bucket = chosenHeap;
			plan.placements[idx].offset = chosenOffset;
		}

		return plan;
	}

	RenderGraphAliasReport RenderGraph::AnalyzeMemoryAliasing()
	{
		SortPasses();

		struct Lifetime
		{
			u32  firstPass = 0;
			u32  lastPass = 0;
			bool firstPassWrites = false;
			bool firstPassReads = false;
			bool used = false;
		};

		HashMap<StringView, Lifetime> lifetimes;

		for (u32 passIndex = 0; passIndex < passes.Size(); ++passIndex)
		{
			RenderGraphPass* pass = passes[passIndex];
			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
				StringView                         resourceName = dependency.name;
				if (const Resource* dep = FindResource(resourceName))
				{
					if (dep->kind == Resource::Kind::View)
					{
						resourceName = dep->viewDesc.texture;
					}
				}

				Lifetime&  lifetime = lifetimes[resourceName];
				const bool writes = WritesResource(dependency.access);
				const bool reads = ReadsResource(dependency.access);

				if (!lifetime.used)
				{
					lifetime.firstPass = passIndex;
					lifetime.lastPass = passIndex;
					lifetime.firstPassWrites = writes;
					lifetime.firstPassReads = reads;
					lifetime.used = true;
				}
				else
				{
					if (passIndex == lifetime.firstPass)
					{
						lifetime.firstPassWrites = lifetime.firstPassWrites || writes;
						lifetime.firstPassReads = lifetime.firstPassReads || reads;
					}
					lifetime.lastPass = Math::Max(lifetime.lastPass, passIndex);
				}
			}
		}

		Array<StringView> names;
		for (const auto& it : lifetimes)
		{
			names.EmplaceBack(it.first);
		}
		Sort(names.Data(), names.Data() + names.Size(), [](StringView a, StringView b)
		{
			return a < b;
		});

		RenderGraphAliasReport report;

		for (StringView name : names)
		{
			const Resource* res = FindResource(name);
			if (res == nullptr || res->kind != Resource::Kind::Texture) continue;
			if (res->textureDesc.pingPong) continue;
			if (name.Compare(colorOutputName) == 0 || name.Compare(depthOutputName) == 0) continue;

			auto            lifetimeIt = lifetimes.Find(name);
			const Lifetime& lifetime = lifetimeIt->second;
			if (!lifetime.firstPassWrites || lifetime.firstPassReads) continue;

			TextureDesc               desc = BuildTextureDesc(name, *res);
			TextureMemoryRequirements requirements = device->GetTextureMemoryRequirements(desc);
			if (requirements.size == 0) continue;

			RenderGraphAliasResource aliasResource;
			aliasResource.firstPass = lifetime.firstPass;
			aliasResource.lastPass = lifetime.lastPass;
			aliasResource.size = requirements.size;
			aliasResource.alignment = requirements.alignment;
			aliasResource.memoryTypeBits = requirements.memoryTypeBits;

			report.resourceNames.EmplaceBack(name);
			report.resources.EmplaceBack(aliasResource);
			report.standaloneBytes += requirements.size;
		}

		report.plan = ComputeRenderGraphAliasPlan(report.resources);

		for (const RenderGraphAliasBucket& bucket : report.plan.buckets)
		{
			report.aliasedBytes += bucket.size;
		}

		return report;
	}

	void RenderGraph::BuildAliasGroup()
	{
		DestroyAliasGroup();

		RenderGraphAliasReport report = AnalyzeMemoryAliasing();
		if (report.resourceNames.Empty()) return;

		logger.Debug("memory aliasing: {} transient textures in {} heap(s), {} KB -> {} KB",
		             report.resourceNames.Size(), report.plan.buckets.Size(), report.standaloneBytes / 1024, report.aliasedBytes / 1024);

		aliasHeaps.Resize(report.plan.buckets.Size(), nullptr);
		for (u32 b = 0; b < report.plan.buckets.Size(); ++b)
		{
			const RenderGraphAliasBucket& bucket = report.plan.buckets[b];
			aliasHeaps[b] = device->CreateMemory(bucket.size, bucket.alignment, bucket.memoryTypeBits);
		}

		for (u32 i = 0; i < report.resourceNames.Size(); ++i)
		{
			const String&                    name = report.resourceNames[i];
			const RenderGraphAliasPlacement& placement = report.plan.placements[i];

			Resource* res = FindResource(name);
			if (res == nullptr || aliasHeaps[placement.bucket] == nullptr) continue;

			TextureDesc desc = BuildTextureDesc(name, *res);
			res->textures[0] = device->CreateAliasedTexture(desc, aliasHeaps[placement.bucket], placement.offset);
			res->states[0] = ResourceState::Undefined;
			res->textureStates[0].Resize(TextureMipCount(res->textures[0]) * TextureLayerCount(res->textures[0]), ResourceState::Undefined);
			res->textureScopes[0].Resize(res->textureStates[0].Size(), BarrierSyncScope::Automatic);
			res->textureLastWrites[0].Resize(res->textureStates[0].Size(), false);
			res->aliased = true;
		}
	}

	void RenderGraph::DestroyAliasGroup()
	{
		if (aliasHeaps.Empty()) return;

		device->WaitIdle();

		for (auto& it : framebufferCache)
		{
			it.second->Destroy();
		}
		framebufferCache.Clear();

		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (res.kind != Resource::Kind::View || res.view == nullptr) continue;

			Resource* source = FindResource(res.viewDesc.texture);
			if (source != nullptr && source->aliased)
			{
				res.view->Destroy();
				res.view = nullptr;
			}
		}

		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (!res.aliased) continue;

			if (res.textures[0] != nullptr)
			{
				res.textures[0]->Destroy();
				res.textures[0] = nullptr;
			}
			res.states[0] = ResourceState::Undefined;
			res.textureStates[0].Clear();
			res.textureScopes[0].Clear();
			res.textureLastWrites[0].Clear();
			res.aliased = false;
		}

		for (GPUMemory* heap : aliasHeaps)
		{
			if (heap != nullptr) heap->Destroy();
		}
		aliasHeaps.Clear();
	}

	void RenderGraph::CollectUnusedResources()
	{
		bool anyStale = false;
		for (auto& it : resources)
		{
			if (it.second.kind == Resource::Kind::Instance || it.second.aliased) continue;
			if (frameGeneration - it.second.lastUsed >= SK_FRAMES_IN_FLIGHT)
			{
				anyStale = true;
				break;
			}
		}

		if (!anyStale) return;

		device->WaitIdle();

		for (auto& it : framebufferCache)
		{
			it.second->Destroy();
		}
		framebufferCache.Clear();

		Array<String> toRemove;
		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (res.kind == Resource::Kind::Instance || res.aliased) continue;
			if (frameGeneration - res.lastUsed < SK_FRAMES_IN_FLIGHT) continue;

			for (GPUTexture*& texture : res.textures)
			{
				if (texture != nullptr)
				{
					texture->Destroy();
					texture = nullptr;
				}
			}

			if (res.view != nullptr)
			{
				res.view->Destroy();
				res.view = nullptr;
			}

			for (GPUBuffer*& buffer : res.buffers)
			{
				if (buffer != nullptr)
				{
					buffer->Destroy();
					buffer = nullptr;
				}
			}

			toRemove.EmplaceBack(it.first);
		}

		for (const String& name : toRemove)
		{
			resources.Erase(name);
		}
	}

	void RenderGraph::DestroyOutputFollowingResources()
	{
		device->WaitIdle();

		for (auto& it : framebufferCache)
		{
			it.second->Destroy();
		}
		framebufferCache.Clear();

		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (res.kind == Resource::Kind::Texture)
			{
				bool followsOutput = res.textureDesc.extent.width == 0 && res.textureDesc.extent.height == 0;
				if (!followsOutput) continue;

				for (u32 i = 0; i < 2; ++i)
				{
					if (res.textures[i] != nullptr)
					{
						res.textures[i]->Destroy();
						res.textures[i] = nullptr;
					}
					res.states[i] = ResourceState::Undefined;
					res.textureStates[i].Clear();
					res.textureScopes[i].Clear();
					res.textureLastWrites[i].Clear();
				}
			}
			else if (res.kind == Resource::Kind::View)
			{
				if (res.view != nullptr)
				{
					res.view->Destroy();
					res.view = nullptr;
				}
			}
		}
	}

	void RenderGraph::CreateRenderPasses()
	{
		for (RenderGraphPass* pass : passes)
		{
			if (pass->type == RenderGraphPassType::Compute || pass->type == RenderGraphPassType::Raytrace)
			{
				if (!pass->shader) continue;

				usize pipelineKey = 0;
				HashCombine(pipelineKey, HashValue(pass->shader));
				HashCombine(pipelineKey, static_cast<usize>(pass->type));

				if (auto it = pipelineCache.Find(pipelineKey))
				{
					pass->pipeline = it->second;
					continue;
				}

				GPUPipeline* pipeline = nullptr;
				if (pass->type == RenderGraphPassType::Compute)
				{
					ComputePipelineDesc pipelineDesc;
					pipelineDesc.shader = pass->shader;
					pipelineDesc.debugName = pass->name;
					pipeline = device->CreateComputePipeline(pipelineDesc);
				}
				else
				{
					RayTracingPipelineDesc pipelineDesc;
					pipelineDesc.shader = pass->shader;
					pipelineDesc.debugName = pass->name;
					pipeline = device->CreateRayTracingPipeline(pipelineDesc);
				}

				ownedPipelines.EmplaceBack(pipeline);
				pipelineCache.Insert(pipelineKey, pipeline);
				pass->pipeline = pipeline;
				continue;
			}

			if (pass->type != RenderGraphPassType::Graphics) continue;

			Array<RenderPassAttachment>& attachments = renderPassAttachmentsScratch;
			attachments.Clear();

			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
				if (!WritesResource(dependency.access)) continue;

				Resource* res = FindResource(dependency.name);
				if (res == nullptr) continue;

				GPUTexture*     texture = nullptr;
				GPUTextureView* view = nullptr;

				if (res->kind == Resource::Kind::Texture)
				{
					const u32 slot = res->textures[1] != nullptr ? currentFrame : 0;
					texture = res->textures[slot];
					if (texture != nullptr) view = texture->GetTextureView();
				}
				else if (res->kind == Resource::Kind::Imported)
				{
					if (currentOutputIndex < res->imported.Size()) texture = res->imported[currentOutputIndex];
					if (texture != nullptr) view = texture->GetTextureView();
				}
				else if (res->kind == Resource::Kind::View)
				{
					view = res->view;
					if (view != nullptr) texture = view->GetTexture();
				}

				if (texture == nullptr || view == nullptr) continue;

				const TextureDesc& textureDesc = texture->GetDesc();
				const bool         isDepth = IsDepthFormat(textureDesc.format);

				RenderPassAttachment attachment;
				attachment.desc.format = textureDesc.format;
				attachment.desc.sampleCount = textureDesc.sampleCount;
				attachment.desc.initialState = isDepth ? ResourceState::DepthStencilAttachment : ResourceState::ColorAttachment;
				attachment.desc.finalState = attachment.desc.initialState;
				attachment.desc.loadOp = dependency.access == RenderGraphAccess::Write ? AttachmentLoadOp::Clear : AttachmentLoadOp::Load;
				attachment.desc.storeOp = AttachmentStoreOp::Store;
				attachment.view = view;
				attachment.resolve = pass->HasResolve(dependency.name);
				attachments.EmplaceBack(attachment);
			}

			if (attachments.Empty())
			{
				pass->renderPass = nullptr;
				pass->framebuffer = nullptr;
				continue;
			}

			usize renderPassKey = 0;
			for (const RenderPassAttachment& attachment : attachments)
			{
				HashCombine(renderPassKey, static_cast<usize>(attachment.desc.format));
				HashCombine(renderPassKey, attachment.desc.sampleCount);
				HashCombine(renderPassKey, static_cast<usize>(attachment.desc.loadOp));
				HashCombine(renderPassKey, static_cast<usize>(attachment.desc.initialState));
				HashCombine(renderPassKey, attachment.resolve ? 1u : 0u);
			}

			GPURenderPass* renderPass = nullptr;
			if (auto it = renderPassCache.Find(renderPassKey))
			{
				renderPass = it->second;
			}
			else
			{
				RenderPassDesc renderPassDesc;
				for (const RenderPassAttachment& attachment : attachments)
				{
					if (attachment.resolve)
					{
						renderPassDesc.resolveAttachments.EmplaceBack(attachment.desc);
					}
					else
					{
						renderPassDesc.attachments.EmplaceBack(attachment.desc);
					}
				}
				renderPassDesc.debugName = pass->name;
				renderPass = device->CreateRenderPass(renderPassDesc);
				renderPassCache.Insert(renderPassKey, renderPass);
			}

			usize framebufferKey = renderPassKey;
			HashCombine(framebufferKey, reinterpret_cast<usize>(renderPass));
			for (const RenderPassAttachment& attachment : attachments)
			{
				HashCombine(framebufferKey, reinterpret_cast<usize>(attachment.view));
			}

			GPUFramebuffer* framebuffer = nullptr;
			if (auto it = framebufferCache.Find(framebufferKey))
			{
				framebuffer = it->second;
			}
			else
			{
				FramebufferDesc framebufferDesc;
				framebufferDesc.renderPass = renderPass;
				for (const RenderPassAttachment& attachment : attachments)
				{
					framebufferDesc.attachments.EmplaceBack(attachment.view);
				}
				framebufferDesc.debugName = pass->name;
				framebuffer = device->CreateFramebuffer(framebufferDesc);
				framebufferCache.Insert(framebufferKey, framebuffer);
			}

			pass->renderPass = renderPass;
			pass->framebuffer = framebuffer;
		}
	}

	void RenderGraph::UpdateSceneBuffer()
	{
		if (sceneBuffer == nullptr) return;

		u32 instanceCount = 0;
		GPUBuffer* instanceBuffer = nullptr;
		GPUTopLevelAS* tlas = nullptr;
		if (currentScene != nullptr)
		{
			instanceCount = currentScene->renderObjects.instanceDataCount;
			instanceBuffer = currentScene->renderObjects.instanceDataBuffer;
			tlas = currentScene->renderObjects.tlas;
		}

		u8* data = static_cast<u8*>(sceneBuffer->GetMappedData());
		GlobalSceneBuffer* sceneBufferData = new(data + currentFrame * sceneBufferFrameSize) GlobalSceneBuffer{
			.viewProjection = camera.viewProjection,
			.view = camera.view,
			.projection = camera.projection,
			.viewInv = camera.invView,
			.projectionInv = camera.invProjection,
			.viewProjInv = camera.invViewProj,
			.cameraPosition = camera.cameraPosition,
			.farClip = camera.farClip,
			.outputSize = IVec2{static_cast<i32>(outputSize.width), static_cast<i32>(outputSize.height)},
			.jitter = camera.jitter,
			.prevJitter = camera.previousJitter,
			.instanceCount = instanceCount,
			.cullingMask = camera.cullingMask,
		};

		for (u32 i = 0; i < 6; ++i)
		{
			const Plane& plane = camera.frustum.planes[i];
			sceneBufferData->frustumPlanes[i] = Vec4{plane.normal.x, plane.normal.y, plane.normal.z, plane.distance};
		}

		if (instanceBuffer != nullptr)
		{
			sceneDescriptorSets[currentFrame]->UpdateBuffer(1, instanceBuffer, 0, 0);
		}

		if (device->GetFeatures().rayTracing && tlas != nullptr)
		{
			DescriptorUpdate tlasUpdate;
			tlasUpdate.type = DescriptorType::AccelerationStructure;
			tlasUpdate.binding = 6;
			tlasUpdate.topLevelAS = tlas;
			sceneDescriptorSets[currentFrame]->Update(tlasUpdate);
		}
	}

	void RenderGraph::Begin(Scene* scene)
	{
		currentScene = scene;
		prevFrame = currentFrame;
		currentFrame = (currentFrame + 1) % SK_FRAMES_IN_FLIGHT;
		++frameGeneration;

		for (RenderGraphPass* pass : passes)
		{
			passPool.EmplaceBack(pass);
		}
		passes.Clear();
		passesSorted = false;

		CollectUnusedResources();
	}

	void RenderGraph::Resize(Extent newExtent)
	{
		SetOutputSize(newExtent);
	}

	void RenderGraph::Execute(GPUCommandBuffer* cmd)
	{
		CreateSceneResources();

		SortPasses();

		const bool resized = sizeChanged;
		if (sizeChanged)
		{
			DestroyOutputFollowingResources();
			sizeChanged = false;
			resourcesDirty = true;
		}

		if (resourcesDirty)
		{
			CreateResourceTextures();
			resourcesDirty = false;
		}

		if (resized)
		{
			for (RenderGraphPass* pass : passes)
			{
				if (pass->resizeFn)
				{
					pass->resizeFn(*this, outputSize);
				}
			}
		}

		CreateRenderPasses();
		UpdateSceneBuffer();

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

		for (auto& it : resources)
		{
			Resource& res = it.second;
			if (!res.aliased) continue;

			res.states[0] = ResourceState::Undefined;
			for (ResourceState& state : res.textureStates[0]) state = ResourceState::Undefined;
			for (BarrierSyncScope& scope : res.textureScopes[0]) scope = BarrierSyncScope::Automatic;
			for (bool& lastWrite : res.textureLastWrites[0]) lastWrite = false;
		}

		passActivatesAliasScratch.Clear();
		passActivatesAliasScratch.Resize(passes.Size(), false);
		activatedAliasResourcesScratch.Clear();
		for (u32 passIndex = 0; passIndex < passes.Size(); ++passIndex)
		{
			RenderGraphPass* pass = passes[passIndex];
			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
				StringView                         resourceName = dependency.name;
				if (const Resource* dep = FindResource(resourceName))
				{
					if (dep->kind == Resource::Kind::View)
					{
						resourceName = dep->viewDesc.texture;
					}
				}

				const Resource* res = FindResource(resourceName);
				if (res == nullptr || !res->aliased) continue;

				bool alreadyActivated = false;
				for (const Resource* activated : activatedAliasResourcesScratch)
				{
					if (activated == res)
					{
						alreadyActivated = true;
						break;
					}
				}
				if (alreadyActivated) continue;

				activatedAliasResourcesScratch.EmplaceBack(res);
				passActivatesAliasScratch[passIndex] = true;
			}
		}

		for (u32 passIndex = 0; passIndex < passes.Size(); ++passIndex)
		{
			RenderGraphPass* pass = passes[passIndex];

			if (passActivatesAliasScratch[passIndex])
			{
				cmd->MemoryBarrier();
			}

			const BarrierSyncScope passScope = PassSyncScope(pass->type);

			for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
			{
				const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
				Resource* res = FindResource(dependency.name);
				if (res == nullptr) continue;

				res->lastUsed = frameGeneration;
				if (res->kind == Resource::Kind::View)
				{
					if (Resource* source = FindResource(res->viewDesc.texture))
					{
						source->lastUsed = frameGeneration;
					}
				}

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

			const bool useRenderPass = pass->type == RenderGraphPassType::Graphics && pass->renderPass != nullptr && pass->framebuffer != nullptr;

			if (useRenderPass)
			{
				ClearValues clearValues = {};
				for (usize dependencyIndex = 0; dependencyIndex < pass->dependencyCount; ++dependencyIndex)
				{
					const RenderGraphPass::Dependency& dependency = pass->dependencies[dependencyIndex];
					if (!WritesResource(dependency.access)) continue;
					const Resource* res = FindResource(dependency.name);
					if (res != nullptr && res->kind == Resource::Kind::Texture && !IsDepthFormat(res->textureDesc.format))
					{
						clearValues.color = res->textureDesc.clearColor;
						break;
					}
				}

				BeginRenderPassInfo info;
				info.renderPass = pass->renderPass;
				info.framebuffer = pass->framebuffer;
				info.clearValues = &clearValues;
				cmd->BeginRenderPass(info);

				const Extent extent = pass->framebuffer->GetExtent();

				ViewportInfo viewport{};
				viewport.x = 0.0f;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				if (device->GetAPI() == GraphicsAPI::Vulkan && pass->invertViewport)
				{
					viewport.y = static_cast<f32>(extent.height);
					viewport.width = static_cast<f32>(extent.width);
					viewport.height = -static_cast<f32>(extent.height);
				}
				else
				{
					viewport.y = 0.0f;
					viewport.width = static_cast<f32>(extent.width);
					viewport.height = static_cast<f32>(extent.height);
				}
				cmd->SetViewport(viewport);
				cmd->SetScissor({0, 0}, extent);
			}

			if (pass->pipeline != nullptr)
			{
				cmd->BindPipeline(pass->pipeline);

				for (usize descriptorSetIndex = 0; descriptorSetIndex < pass->boundDescriptorSetCount; ++descriptorSetIndex)
				{
					const RenderGraphPass::BoundDescriptorSet& binding = pass->boundDescriptorSets[descriptorSetIndex];
					cmd->BindDescriptorSet(pass->pipeline, binding.set, binding.descriptorSet);
				}

				if (pass->constantsFn && pass->constantsSize > 0)
				{
					u8 constantsData[256] = {};
					SK_ASSERT(pass->constantsSize <= sizeof(constantsData), "render graph push constants exceed scratch buffer");
					pass->constantsFn(*this, constantsData);
					cmd->PushConstants(pass->pipeline, pass->constantsStages, 0, pass->constantsSize, constantsData);
				}
			}

			if (pass->renderFn)
			{
				pass->renderFn(*this, currentScene, cmd);
			}
			else if (pass->pipeline != nullptr)
			{
				if (pass->type == RenderGraphPassType::Compute)
				{
					if (pass->indirectBuffer != nullptr)
					{
						cmd->DispatchIndirect(pass->indirectBuffer, 0);
					}
					else
					{
						const Extent3D groups = DispatchGroupCount(pass->pipeline, pass->dispatchX, pass->dispatchY, pass->dispatchZ);
						cmd->Dispatch(groups.width, groups.height, groups.depth);
					}
				}
				else if (pass->type == RenderGraphPassType::Raytrace)
				{
					cmd->TraceRays(pass->pipeline, pass->dispatchX, pass->dispatchY, pass->dispatchZ);
				}
			}

			if (useRenderPass)
			{
				cmd->EndRenderPass();
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
