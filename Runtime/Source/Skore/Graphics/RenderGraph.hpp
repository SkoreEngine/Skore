#pragma once

#include "Device.hpp"
#include "GraphicsCommon.hpp"
#include "Graphics.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Traits.hpp"

#include <functional>

namespace Skore
{
	class Scene;
	class RenderGraph;
	class RenderGraphPass;

	enum class RenderGraphPassType
	{
		Compute  = 0,
		Graphics = 1,
		Raytrace = 2,
		Transfer = 3,
	};

	enum class RenderGraphAccess
	{
		Read      = 0,
		Write     = 1,
		ReadWrite = 2,
	};

	struct RenderGraphTextureDesc
	{
		Format        format = Format::Unknown;
		Extent        extent{};
		Vec2          scale = Vec2(1.0f);
		u32           arrayLayers = 1;
		u32           samples = 1;
		u32           mipLevels = 1;
		ResourceUsage usage = ResourceUsage::None;
		Vec4          clearColor = Vec4(0.0f);
		bool          cubemap = false;
		bool          pingPong = false;
	};

	struct RenderGraphBufferDesc
	{
		usize         size = 0;
		ResourceUsage usage = ResourceUsage::None;
		bool          hostVisible = true;
		bool          persistentMapped = false;
		bool          perFrame = false;
		bool          pingPong = false;
	};

	struct RenderGraphAccelStructDesc
	{
		GPUTopLevelAS* topLevelAS = nullptr;
	};

	struct RenderGraphViewDesc
	{
		String          texture;
		TextureViewType viewType = TextureViewType::Type2D;
		u32             baseMipLevel = 0;
		u32             mipLevelCount = 1;
		u32             baseArrayLayer = 0;
		u32             arrayLayerCount = 1;
	};

	struct RenderGraphAliasResource
	{
		u32 firstPass = 0;
		u32 lastPass = 0;
		u64 size = 0;
		u64 alignment = 0;
		u32 memoryTypeBits = 0;
	};

	struct RenderGraphAliasPlacement
	{
		u32 bucket = 0;
		u64 offset = 0;
	};

	struct RenderGraphAliasBucket
	{
		u64 size = 0;
		u64 alignment = 0;
		u32 memoryTypeBits = 0;
	};

	struct RenderGraphAliasPlan
	{
		Array<RenderGraphAliasBucket>    buckets;
		Array<RenderGraphAliasPlacement> placements;
	};

	SK_API RenderGraphAliasPlan ComputeRenderGraphAliasPlan(const Array<RenderGraphAliasResource>& resources);

	struct RenderGraphAliasReport
	{
		Array<String>                   resourceNames;
		Array<RenderGraphAliasResource> resources;
		RenderGraphAliasPlan            plan;
		u64                             standaloneBytes = 0;
		u64                             aliasedBytes = 0;
	};

	class SK_API RenderGraphPass
	{
	public:
		RenderGraphPass& Write(StringView name);
		RenderGraphPass& Read(StringView name);
		RenderGraphPass& WriteRead(StringView name);
		RenderGraphPass& Resolve(StringView name);

		RenderGraphPass& DescriptorSet(u32 set, GPUDescriptorSet* ds);

		RenderGraphPass& InvertViewport(bool value = true);
		RenderGraphPass& RequireJitter(bool value = true);
		RenderGraphPass& RequireMotionVector(bool value = true);
		RenderGraphPass& ClearColor(StringView name, const Vec4& color);
		RenderGraphPass& Stage(i32 stage);

		template <typename T>
		RenderGraphPass& Constants(std::function<void(RenderGraph& rg, T& constants)> f)
		{
			return SetConstants(sizeof(T), ShaderStage::All, [f](RenderGraph& rg, void* ptr)
			{
				f(rg, *static_cast<T*>(ptr));
			});
		}

		RenderGraphPass& Resize(std::function<void(RenderGraph& rg, Extent newExtent)> f);
		RenderGraphPass& Render(std::function<void(RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)> fn);

		RenderGraphPass& Dispatch(u32 x, u32 y, u32 z);
		RenderGraphPass& Dispatch(Extent3D extent);
		RenderGraphPass& DispatchIndirect(GPUBuffer* indirectBuffer);
		RenderGraphPass& TraceRays(u32 width, u32 height, u32 depth);

		RenderGraph*   GetGraph() const;
		GPUPipeline*   GetPipeline() const;
		GPURenderPass* GetRenderPass() const;

	private:
		friend class RenderGraph;

		struct Dependency
		{
			String            nameStorage;
			StringView        name;
			RenderGraphAccess access = RenderGraphAccess::Read;
		};

		struct ResolveTarget
		{
			String     nameStorage;
			StringView name;
		};

		struct BoundDescriptorSet
		{
			u32               set = 0;
			GPUDescriptorSet* descriptorSet = nullptr;
		};

		RenderGraphPass& SetConstants(u32 size, ShaderStage stages, std::function<void(RenderGraph&, void*)> f);
		void             Reset(RenderGraph* graph, StringView name, RenderGraphPassType type);
		void             AddDependency(StringView name, RenderGraphAccess access);
		void             AddResolve(StringView name);
		void             SetNameReference(String& storage, StringView& view, StringView name);
		bool             HasResolve(StringView name) const;

		RenderGraph*        graph = nullptr;
		String              name;
		RenderGraphPassType type = RenderGraphPassType::Compute;
		RID                 shader;
		GPUPipeline*        pipeline = nullptr;
		GPURenderPass*      renderPass = nullptr;
		GPUFramebuffer*     framebuffer = nullptr;

		Array<Dependency>         dependencies;
		usize                     dependencyCount = 0;
		Array<ResolveTarget>      resolves;
		usize                     resolveCount = 0;
		Array<BoundDescriptorSet> boundDescriptorSets;
		usize                     boundDescriptorSetCount = 0;

		bool invertViewport = false;
		bool requireJitter = false;
		bool requireMotionVector = false;
		i32  stage = 0;

		u32        dispatchX = 0;
		u32        dispatchY = 0;
		u32        dispatchZ = 0;
		GPUBuffer* indirectBuffer = nullptr;

		u32                                      constantsSize = 0;
		ShaderStage                              constantsStages = ShaderStage::All;
		std::function<void(RenderGraph&, void*)> constantsFn;

		std::function<void(RenderGraph&, Extent)>                        resizeFn;
		std::function<void(RenderGraphPass&, Scene*, GPUCommandBuffer*)> renderFn;
	};

	class SK_API RenderGraph
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(RenderGraph);

		RenderGraph(GPUDevice* device = Graphics::GetDevice());
		~RenderGraph();

		GPUDevice* GetDevice() const;

		void Create(StringView name, const RenderGraphTextureDesc& textureDesc);
		void Create(StringView name, const RenderGraphBufferDesc& bufferDesc);
		void Create(StringView name, const RenderGraphAccelStructDesc& accelStructDesc);
		void CreateView(StringView name, const RenderGraphViewDesc& viewDesc);

		template <typename F>
		GPUPipeline* GetOrCreatePipeline(StringView key, F&& create)
		{
			return GetOrCreatePipeline(key, [](VoidPtr userData) -> GPUPipeline*
			{
				return (*static_cast<Traits::RemoveReference<F>*>(userData))();
			}, &create);
		}

		void Import(StringView name, Span<GPUTexture*> textures, ResourceState state);

		RenderGraphPass& AddComputePass(StringView name, StringView shaderPath);
		RenderGraphPass& AddComputePass(StringView name, RID shaderRID);
		RenderGraphPass& AddRaytracePass(StringView name, RID shaderRID);
		RenderGraphPass& AddGraphicsPass(StringView name);
		RenderGraphPass& AddPass(StringView name);

		GPUTexture*     GetTexture(StringView name) const;
		GPUTexture*     GetPrevTexture(StringView name) const;
		GPUTextureView* GetTextureView(StringView name) const;
		GPUBuffer*      GetBuffer(StringView name) const;
		GPUBuffer*      GetPrevBuffer(StringView name) const;
		GPUTopLevelAS*  GetTopLevelAS(StringView name) const;

		ResourceUsage InferTextureUsage(StringView name) const;

		RenderGraphAliasReport AnalyzeMemoryAliasing();

		VoidPtr CreateInstance(StringView name, usize size);
		VoidPtr GetInstanceData(StringView name) const;

		template <typename T>
		T* CreateInstance(StringView name)
		{
			return static_cast<T*>(CreateInstance(name, sizeof(T)));
		}

		template <typename T>
		T* GetInstanceData(StringView name) const
		{
			return static_cast<T*>(GetInstanceData(name));
		}


		GPUDescriptorSet* GetDescriptorSet(RID shader, StringView variant, u32 set);

		GPUDescriptorSet* GetSceneDescriptorSet() const;
		GPUDescriptorSet* GetSceneDescriptorSet(u32 frame) const;
		GPUBuffer*        GetSceneBuffer() const;
		u64               GetSceneBufferOffset() const;
		u64               GetSceneBufferSize() const;

		u32 GetCurrentFrame() const;
		u32 GetPrevFrame() const;
		u32 GetTopologyBuildCount() const;

		void UpdateCamera(f32 nearClip, f32 farClip, f32 fov, Projection projection, const Mat4& view, const Vec3& cameraPosition, bool updateFrustum = true);

		void        SetColorOutput(StringView name);
		void        SetDepthOutput(StringView name);
		void        SetOutputAttachments(StringView name, Span<GPUTexture*> attachments, ResourceState requiredState);
		void        SetCurrentOutputIndex(u32 index);
		void        SetOutputSize(Extent size);
		Extent      GetOutputSize() const;
		GPUTexture* GetColorOutput() const;
		GPUTexture* GetDepthOutput() const;

		Scene* GetScene() const;

		void Begin(Scene* scene);
		void Resize(Extent newExtent);
		void Execute(GPUCommandBuffer* cmd);

		struct CameraData
		{
			f32 nearClip = 0.0f;
			f32 farClip = 0.0f;

			Mat4 view = Mat4(1.0);
			Mat4 invView = Mat4(1.0);
			Mat4 projection = Mat4(1.0);
			Mat4 invProjection = Mat4(1.0);
			Mat4 viewProjection = Mat4(1.0);
			Mat4 invViewProj = Mat4(1.0);
			Mat4 previousViewProjection = Mat4(1.0);

			Mat4 projectionNoJitter = Mat4(1.0);
			Mat4 viewProjectionNoJitter = Mat4(1.0);
			Mat4 previousViewProjectionNoJitter = Mat4(1.0);

			Vec3 cameraPosition = {};

			i32  jitterIndex = 0;
			i32  jitterPeriod = 8;
			Vec2 jitter{};
			Vec2 previousJitter{};

			Frustum frustum;
			u64     cullingMask = ~0ULL;
		} camera;

	private:
		friend class RenderGraphPass;

		struct Resource
		{
			enum class Kind
			{
				Texture,
				Buffer,
				View,
				Imported,
				Instance,
				AccelerationStructure,
			};

			Kind                   kind = Kind::Texture;
			RenderGraphTextureDesc textureDesc;
			RenderGraphBufferDesc  bufferDesc;
			RenderGraphViewDesc    viewDesc;

			GPUTexture*     textures[2] = {nullptr, nullptr};
			ResourceState   states[2] = {ResourceState::Undefined, ResourceState::Undefined};
			Array<ResourceState> textureStates[2];
			Array<BarrierSyncScope> textureScopes[2];
			Array<bool>          textureLastWrites[2];
			GPUTextureView* view = nullptr;
			GPUBuffer*      buffers[SK_FRAMES_IN_FLIGHT] = {};
			ResourceState   bufferStates[SK_FRAMES_IN_FLIGHT] = {};
			BarrierSyncScope bufferScopes[SK_FRAMES_IN_FLIGHT] = {};
			bool            bufferLastWrites[SK_FRAMES_IN_FLIGHT] = {};

			Array<GPUTexture*> imported;
			ResourceState      importedState = ResourceState::Undefined;
			Array<Array<ResourceState>> importedStates;
			Array<Array<BarrierSyncScope>> importedScopes;
			Array<Array<bool>>          importedLastWrites;

			VoidPtr instanceData = nullptr;
			usize   instanceSize = 0;

			GPUTopLevelAS* topLevelAS = nullptr;

			u64  lastUsed = 0;
			bool aliased = false;
		};

		struct RenderPassAttachment
		{
			AttachmentDesc  desc;
			GPUTextureView* view = nullptr;
			bool            resolve = false;
		};

		RenderGraphPass& EmplacePass(StringView name, RenderGraphPassType type);
		Resource*        FindResource(StringView name);
		const Resource*  FindResource(StringView name) const;
		StringView       FindResourceName(StringView name) const;
		GPUPipeline*     GetOrCreatePipeline(StringView key, GPUPipeline* (*create)(VoidPtr userData), VoidPtr userData);

		GPUDescriptorSet* GetAutoDescriptorSet(const RenderGraphPass* pass, u32 set);
		void              BindAutoDescriptorSet(RenderGraphPass* pass, GPUCommandBuffer* cmd);

		void CreateSceneResources();
		void SortPasses();
		TextureDesc BuildTextureDesc(StringView name, const Resource& res) const;
		void CreateResourceTextures();
		void BuildAliasGroup();
		void DestroyAliasGroup();
		void DestroyOutputFollowingResources();
		void CollectUnusedResources();
		void CreateRenderPasses();
		void UpdateSceneBuffer();
		ResourceUsage InferBufferUsage(StringView name) const;

		GPUDevice* device = nullptr;
		Scene*     currentScene = nullptr;
		Extent     outputSize{};

		u32 currentFrame = 0;
		u32 prevFrame = 0;
		u32 currentOutputIndex = 0;
		u64 frameGeneration = 0;

		String colorOutputName;
		String depthOutputName;

		GPUBuffer*        sceneBuffer = nullptr;
		GPUDescriptorSet* sceneDescriptorSets[SK_FRAMES_IN_FLIGHT] = {nullptr, nullptr};
		u64               sceneBufferFrameSize = 0;

		HashMap<String, Resource>         resources;
		Array<RenderGraphPass*>           passes;
		Array<RenderGraphPass*>           passPool;
		Array<u32>                        cachedSortedPassIndices;
		Array<RenderGraphPass*>           sortedPassScratch;
		Array<RenderPassAttachment>       renderPassAttachmentsScratch;
		Array<bool>                       passActivatesAliasScratch;
		Array<const Resource*>            activatedAliasResourcesScratch;
		HashMap<usize, GPUPipeline*>      pipelineCache;
		HashMap<usize, GPUDescriptorSet*> descriptorSetCache;
		HashMap<usize, GPUDescriptorSet*> autoDescriptorSetCache;
		HashMap<usize, GPURenderPass*>    renderPassCache;
		HashMap<usize, GPUFramebuffer*>   framebufferCache;
		Array<GPUPipeline*>               ownedPipelines;
		Array<GPUMemory*>                 aliasHeaps;

		bool resourcesDirty = true;
		bool sizeChanged = false;
		bool passesSorted = false;
		bool passGraphCacheValid = false;
		usize passGraphSignature = 0;
		u32 topologyBuildCount = 0;
	};
}
