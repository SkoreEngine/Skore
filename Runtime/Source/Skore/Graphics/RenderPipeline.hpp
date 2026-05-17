#pragma once

#include "Device.hpp"
#include "GraphicsCommon.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
	class Scene;
	class RenderPipelineContext;
	struct RenderPipeline;
	class GPUCommandBuffer;

	typedef void (*RenderPipelineContextSubmitFn)(GPUCommandBuffer* cmd, VoidPtr userData);

	enum class RenderPipelinePassType
	{
		Other    = 0,
		Graphics = 1,
		Compute  = 2,
		Raytrace = 3
	};

	enum class RenderPipelineTextureAccess
	{
		None      = 0,
		Read      = 1,
		Write     = 2,
		ReadWrite = 3,
	};

	enum class RenderPipelineResourceType
	{
		None        = 0,
		Texture     = 1,
		TextureView = 2,
		Attachment  = 3,
		Buffer      = 4,
		Reference   = 5,
		Instance    = 6,
	};

	struct RenderPipelineResource
	{
		String                     name{};
		RenderPipelineResourceType type = RenderPipelineResourceType::None;

		//texture
		TextureFormat format = TextureFormat::Unknown;
		Extent        extent{};
		Vec2          scale = Vec2(1.0f);
		u32           arrayLayers = 1;
		u32           samples = 1;
		u32           mipLevels = 1;
		ResourceUsage textureUsage = ResourceUsage::None;
		bool					pingPong = false;

		//texture view
		String          textureName;
		TextureViewType viewType = TextureViewType::Undefined;
		u32             baseMipLevel = 0;
		u32             levelCount = 1;
		u32             baseArrayLayer = 0;
		u32             layerCount = 1;

		//attachment
		Vec4 clearColor = Vec4(0.0f);

		//buffer
		usize         size;
		ResourceUsage usage;
		bool          hostVisible{true};
		bool          persistentMapped{false};

		//Reference
		VoidPtr data = nullptr;

		//instance
		TypeID instanceTypeId;
	};

	struct RenderPipelinePassDependency
	{
		String                      name{};
		RenderPipelineTextureAccess access = RenderPipelineTextureAccess::None;
	};

	struct RenderPipelinePassSetup
	{
		RenderPipelinePassType              type = RenderPipelinePassType::Other;
		bool                                invertViewport = false;
		bool																requireJitter = false;
		i32                                 stage = 0;
		Array<RenderPipelinePassDependency> dependencies;
		Array<String>                       resolve;
	};

	struct SK_API RenderPipelinePass : Object
	{
		SK_CLASS(RenderPipelinePass, Object);

		RenderPipelineContext* context = nullptr;
		GPURenderPass*         renderPass = nullptr;

		virtual RenderPipelinePassSetup GetPassSetup() = 0;

		//called right after creation, no resources, passes are created at this usage
		virtual void Create() {}

		//called after resource creation (at this stage it's possible to get render pass and resources)
		virtual void Init() {}

		virtual void Update() {}
		virtual void Render(Scene* scene, GPUCommandBuffer* cmd) {}
		virtual void Destroy() {}
		virtual void OnResize(Extent size) {}
	};

	struct RenderPipelineModuleSetup
	{
		Array<TypeID> passes;
		Array<String> resolve;
	};

	struct SK_API RenderPipelineModule : Object
	{
		SK_CLASS(RenderPipelineModule, Object);

		RenderPipelineContext* context = nullptr;

		virtual Array<RenderPipelineResource> GetResources()
		{
			return {};
		}

		virtual RenderPipelineModuleSetup GetSetup() = 0;

		virtual void Init() {}
		virtual void Destroy() {}
		virtual void Update(Scene* scene) {}
	};

	struct RenderPipelineSetup
	{
		Array<TypeID> modules;
	};

	struct RenderPipelineContextSettings
	{
		Extent  initialOutputSize = {};
		VoidPtr userData = nullptr;
	};

	struct SK_API RenderPipeline : Object
	{
		SK_CLASS(RenderPipeline, Object);

		virtual RenderPipelineSetup   GetPipelineSetup() = 0;
		static RenderPipelineContext* CreateContext(TypeID pipelineTypeId, Span<TypeID> extraModules, const RenderPipelineContextSettings& settings = {});
		static RenderPipelineContext* GetMainContext();
		static void                   SetMainContext(RenderPipelineContext* context);
	};


	class SK_API RenderPipelineContext : public Object
	{
	public:
		SK_CLASS(RenderPipelineContext, Object);
		SK_NO_COPY_CONSTRUCTOR(RenderPipelineContext)
		RenderPipelineContext(RenderPipeline* pipeline, Span<TypeID> extraModules, const RenderPipelineContextSettings& settings);

		void UpdateCamera(f32 nearClip, f32 farClip, f32 fov, Projection projection, const Mat4& view, const Vec3& cameraPosition, bool updateFrustum = true);
		void SetTexture(StringView textureName, GPUTexture* texture);

		GPUTexture* GetTexture(StringView textureName);
		GPUTexture* GetPrevTexture(StringView textureName);
		VoidPtr     GetInstanceData(StringView name);
		GPUBuffer*  GetBuffer(StringView name);

		//output
		void SetOutputAttachments(StringView name, Span<GPUTexture*> attachments, ResourceState requiredState);
		void SetOutputViewsPerAttachment(u32 viewCount);
		void SetCurrentOutputIndex(u32 index);
		void SetOutputSize(Extent size);

		Extent GetOutputSize() const
		{
			return outputSize;
		}

		GPUTexture* GetColorOutput() const;
		GPUTexture* GetDepthOutput() const;

		void SetColorOutput(StringView name);
		void SetDepthOutput(StringView name);

		void DisableContext(bool disabled);

		TypeID GetPipelineTypeId() const;

		void Execute(GPUCommandBuffer* cmd, Scene* scene);
		void Destroy();

		struct
		{
			f32 nearClip = 0.0;
			f32 farClip = 0.0;

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
			Vec2 jitterXy = {};
			Vec2 previousJitterXy = {};

			Frustum frustum;

			u64 cullingMask = ~0ULL;
		} camera;

		RenderPipelineContextSettings settings;

		GPUBuffer* sceneBuffer = nullptr;

		template <typename T>
		T* GetInstanceData(StringView name)
		{
			return static_cast<T*>(GetInstanceData(name));
		}

		GPUDescriptorSet* GetSceneDescriptorSet() const;

		u32 GetCurrentFrame() const
		{
			return currentFrame;
		}

		u64 GetSceneBufferOffset() const
		{
			return sceneBufferFrameSize * currentFrame;
		}

		u64 GetSceneBufferSize() const
		{
			return sceneBufferFrameSize;
		}


		struct PipelineResourceStorage
		{
			RenderPipelineResource desc;

			GPUTexture*     textures[2] = {nullptr, nullptr};
			GPUTextureView* textureView = nullptr;
			GPUBuffer*      buffer = nullptr;

			VoidPtr instanceData = nullptr;
			TypeID  instanceTypeId;

			bool ownsResource = false;

			Array<GPUTexture*> outputAttachments;

			ResourceState requiredState = ResourceState::Undefined;
			u32           lastWrite = U32_MAX;

			TextureDesc textureDesc = {};
			bool        textureUseOutputSize = {};

			void Destroy();

			GPUTexture* GetTexture(u8 frame) const
			{
				if (textures[1] == nullptr)
				{
					return textures[0];
				}
				return textures[frame];
			}

			bool HasTexture() const
			{
				return textures[0] != nullptr;
			}

			GPUTexture* GetResourceTexture(u8 frame)
			{
				if (GPUTexture* texture = GetTexture(frame))
				{
					return texture;
				}

				if (textureView)
				{
					return textureView->GetTexture();
				}
				if (!outputAttachments.Empty())
				{
					return outputAttachments[0];
				}

				return nullptr;
			}


			template <typename T>
			void IterateTextures(T&& func)
			{
				if (GPUTexture* texture = textures[0])
				{
					func(texture, 0);
				}
				else if (textureView)
				{
					func(textureView->GetTexture(), 0);
				}
				else if (!outputAttachments.Empty())
				{
					for (int i = 0; i < outputAttachments.Size(); ++i)
					{
						func(outputAttachments[i], i);
					}
				}
			}
		};

	private:
		bool              contextDisabled = false;
		RenderPipeline*   renderPipeline{};
		u64               sceneBufferFrameSize = 0;
		GPUDescriptorSet* sceneDescriptorSets[SK_FRAMES_IN_FLIGHT] = {nullptr, nullptr};
		bool							applyJitter = false;

		struct PassBarrier
		{
			String        resource;
			ResourceState srcState;
			ResourceState dstState;
		};

		struct PassStorage
		{
			String                  name;
			RenderPipelinePass*     pass = nullptr;
			RenderPipelinePassSetup setup;
			RenderPipelineModule*   module;

			Array<GPUFramebuffer*> framebuffers;

			Array<PassBarrier> preBarriers;
			Array<PassBarrier> postBarriers;

			GPUFramebuffer* GetCurrentFramebuffer(u32 index) const;

			void CreateFrameBuffers(RenderPipelineContext* context);
		};

		Array<PassStorage>           passes;
		Array<RenderPipelineModule*> modules;

		Extent outputSize;

		u32 ViewCount = 1;

		u32 currentFrame = 0;
		u32 prevFrame = 0;
		u32 currentOutputIndex = 0;

		String depthOutputName;
		String colorOutputName;


		HashMap<String, PipelineResourceStorage> resources;
		Array<PassBarrier>                       initializationBarriers;
		bool                                     resourceCreated = false;
		bool                                     resourceDirty = false;

		void CreateContextResources();
		void CreatePasses();
		void CreateBarriers();
		void Resize();

		struct ResourceUsageInfo
		{
			u32                         passIndex = U32_MAX;
			RenderPipelineTextureAccess access = RenderPipelineTextureAccess::None;
		};

		ResourceUsageInfo GetNextUsageInfo(u32 currentPass, StringView attachmentName) const;
		ResourceUsageInfo GetPreviousUsageInfo(u32 currentPass, StringView attachmentName) const;
		ResourceUsageInfo GetLastUsageInfo(StringView attachmentName) const;
		ResourceUsageInfo GetFirstUsageInfo(StringView attachmentName) const;

	public:
		Array<PipelineResourceStorage> GetResources() const;
	};
}
