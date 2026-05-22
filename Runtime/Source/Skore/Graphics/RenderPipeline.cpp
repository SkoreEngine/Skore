#include "Skore/Graphics/RenderPipeline.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/App.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Core/Graph.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"


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

	static Logger& logger = Logger::GetLogger("Skore::RenderPipeline", LogLevel::Off);

	RenderPipelineContext::RenderPipelineContext(RenderPipeline* pipeline, Span<TypeID> extraModules, const RenderPipelineContextSettings& pSettings) : settings(pSettings), renderPipeline(pipeline)
	{
		outputSize = settings.initialOutputSize;
		RenderPipelineSetup setup = pipeline->GetPipelineSetup();

		Array<TypeID> modulesIds = setup.modules;
		modulesIds.Insert(modulesIds.end(), extraModules.begin(), extraModules.end());

		modules.Reserve(modulesIds.Size());

		HashSet<TypeID> moduleIdsSet;

		for (TypeID moduleId : modulesIds)
		{
			ReflectType* moduleReflectType = Reflection::FindTypeById(moduleId);
			if (moduleReflectType == nullptr)
			{
				logger.Error("Could not find module type with id {}", moduleId);
				continue;
			}

			if (moduleIdsSet.Has(moduleId))
			{
				logger.Warn("Module {} already registered", moduleReflectType->GetName());
				continue;
			}

			Object* object = moduleReflectType->NewObject();
			if (object == nullptr)
			{
				logger.Error("Could not create module object for type {} ", moduleReflectType->GetName());
			}

			RenderPipelineModule* module = object->SafeCast<RenderPipelineModule>();
			if (module == nullptr)
			{
				logger.Error("Could not cast object to RenderPipelineModule");
				DestroyAndFree(module);
				continue;
			}
			module->context = this;
			modules.EmplaceBack(module);
			moduleIdsSet.Emplace(moduleId);
		}

		modules.ShrinkToFit();

		HashSet<TypeID> passIdsSet;

		struct PassInfo
		{
			RenderPipelinePass*     pass = nullptr;
			String                  name;
			RenderPipelineModule*   module;
			Array<String>           writes;
			Array<String>           reads;
			RenderPipelinePassSetup setup;
		};

		Array<PassInfo> tempPasses;

		for (RenderPipelineModule* module : modules)
		{
			RenderPipelineModuleSetup moduleSetup = module->GetSetup();
			for (TypeID passId : moduleSetup.passes)
			{
				ReflectType* passReflectType = Reflection::FindTypeById(passId);
				if (passReflectType == nullptr)
				{
					logger.Error("Could not find pass type with id {}", passId);
					continue;
				}

				if (passIdsSet.Has(passId))
				{
					logger.Warn("Pass {} already registered", passReflectType->GetName());
					continue;
				}

				passIdsSet.Emplace(passId);

				Object* object = passReflectType->NewObject();
				if (object == nullptr)
				{
					logger.Error("Could not create pass object for type {} ", passReflectType->GetName());
					continue;
				}

				RenderPipelinePass* pass = object->SafeCast<RenderPipelinePass>();
				if (pass == nullptr)
				{
					logger.Error("Could not cast object created with {} to RenderPipelinePass", passReflectType->GetName());
					DestroyAndFree(pass);
					continue;
				}

				pass->context = this;

				PassInfo info;
				info.pass = pass;
				info.module = module;
				info.setup = pass->GetPassSetup();
				info.name = passReflectType->GetSimpleName();

				if (info.setup.requireJitter)
				{
					applyJitter = true;
				}

				for (const auto& dependency : info.setup.dependencies)
				{
					if (dependency.access == RenderPipelineTextureAccess::Write)
					{
						info.writes.EmplaceBack(dependency.name);
						logger.Debug("Pass {} writes to texture {}", info.name, dependency.name);
					}
					else if (dependency.access == RenderPipelineTextureAccess::Read)
					{
						info.reads.EmplaceBack(dependency.name);
						logger.Debug("Pass {} reads from texture {}", info.name, dependency.name);
					}
					else if (dependency.access == RenderPipelineTextureAccess::ReadWrite)
					{
						info.writes.EmplaceBack(dependency.name);
						info.reads.EmplaceBack(dependency.name);
						logger.Debug("Pass {} reads and writes to texture {}", info.name, dependency.name);
					}
				}

				tempPasses.EmplaceBack(info);
			}

			//setup resources
			for (RenderPipelineResource& resource : module->GetResources())
			{
				if (resources.Find(resource.name) == resources.end())
				{
					resources.Insert(resource.name, PipelineResourceStorage{
						                 .desc = resource
					                 });
				}
			}
		}

		Graph<i32, PassStorage> graph;
		for (int i = 0; i < tempPasses.Size(); ++i)
		{
			graph.AddNode(i, PassStorage{
				              .name = tempPasses[i].name,
				              .pass = tempPasses[i].pass,
				              .setup = tempPasses[i].setup,
				              .module = tempPasses[i].module
			              });
		}

		//Build dependency graph
		for (int i = 0; i < tempPasses.Size(); ++i)
		{
			for (int j = 0; j < tempPasses.Size(); j++)
			{
				if (i == j) continue;

				for (const String& written : tempPasses[i].writes)
				{
					for (const String& read : tempPasses[j].reads)
					{
						if (written == read)
						{
							logger.Debug("Pass {} depends on pass {}", tempPasses[j].name, tempPasses[i].name);
							graph.AddEdge(j, i);
							break;
						}
					}
				}
			}
		}

		//sort passes
		passes = graph.Sort();

		//stable sort by stage - insertion sort to preserve topological order within the same stage
		//passes with stage 0 keep their topological position
		for (i32 i = 1; i < passes.Size(); ++i)
		{
			if (passes[i].setup.stage == 0) continue;
			PassStorage temp = passes[i];
			i32 j = i - 1;
			while (j >= 0 && passes[j].setup.stage != 0 && passes[j].setup.stage > temp.setup.stage)
			{
				passes[j + 1] = passes[j];
				--j;
			}
			passes[j + 1] = temp;
		}

		{
			//trying to sort modules using passes. (not perfect, passes on a module don't necessarily run together)
			auto modulesTemp = modules;
			modules.Clear();

			HashSet<RenderPipelineModule*> modulesAdded;

			for (int i = 0; i < passes.Size(); ++i)
			{
				if (!modulesAdded.Has(passes[i].module))
				{
					modules.EmplaceBack(passes[i].module);
					modulesAdded.Emplace(passes[i].module);
				}
			}

			for (RenderPipelineModule* module : modulesTemp)
			{
				if (!modulesAdded.Has(module))
				{
					modules.EmplaceBack(module);;
				}
			}
		}

		//call create function by order
		for (int i = 0; i < passes.Size(); ++i)
		{
			passes[i].pass->Create();
		}

		//at this stage, it should know the output size.
		CreateContextResources();


		//track lastWrite
		for (int i = 0; i < passes.Size(); ++i)
		{
			for (RenderPipelinePassDependency& dependency : passes[i].setup.dependencies)
			{
				if (dependency.access == RenderPipelineTextureAccess::Write || dependency.access == RenderPipelineTextureAccess::ReadWrite)
				{
					if (auto it = resources.Find(dependency.name))
					{
						it->second.lastWrite = i;
					}
				}
			}
		}

		CreatePasses();
		CreateBarriers();

		u64 alignment = Graphics::GetDevice()->GetProperties().limits.minMemoryMapAlignment;
		sceneBufferFrameSize = AlignedSize(sizeof(GlobalSceneBuffer), alignment);

		BufferDesc bufferDesc;
		bufferDesc.size = sceneBufferFrameSize * SK_FRAMES_IN_FLIGHT;
		bufferDesc.usage = ResourceUsage::ConstantBuffer;
		bufferDesc.hostVisible = true;
		bufferDesc.persistentMapped = true;
		sceneBuffer = Graphics::CreateBuffer(bufferDesc);

		// Scene set (space1) — single descriptor set shared by all raster/RT pipelines:
		//   binding 0: GlobalSceneBuffer (camera matrices)
		//   binding 1: Instances (populated by DefaultLightSetupPass)
		//   binding 2: LightBuffer (populated by DefaultLightSetupPass)
		//   binding 3: shadowMapTexture (populated by DefaultLightSetupPass)
		//   binding 4: shadowMapSampler (populated by DefaultLightSetupPass)
		DescriptorSetDesc desc;
		desc.bindings = {
			DescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = DescriptorType::UniformBuffer
			},
			DescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = DescriptorType::StorageBuffer
			},
			DescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = DescriptorType::UniformBuffer
			},
			DescriptorSetLayoutBinding{
				.binding = 3,
				.descriptorType = DescriptorType::SampledImage
			},
			DescriptorSetLayoutBinding{
				.binding = 4,
				.descriptorType = DescriptorType::Sampler
			}
		};
		desc.debugName = "SceneRendererViewport_descriptorSet";

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			sceneDescriptorSets[i] = Graphics::CreateDescriptorSet(desc);

			DescriptorUpdate descriptorUpdate;
			descriptorUpdate.type = DescriptorType::UniformBuffer;
			descriptorUpdate.binding = 0;
			descriptorUpdate.buffer = sceneBuffer;
			descriptorUpdate.bufferOffset = sceneBufferFrameSize * i;
			descriptorUpdate.bufferRange = sizeof(GlobalSceneBuffer);
			sceneDescriptorSets[i]->Update(descriptorUpdate);
		}

		for (auto& module : modules)
		{
			module->Init();
		}

		for (int i = 0; i < passes.Size(); ++i)
		{
			passes[i].pass->Init();
		}
	}

	void RenderPipelineContext::UpdateCamera(f32 nearClip, f32 farClip, f32 fov, Projection projection, const Mat4& view, const Vec3& cameraPosition, bool updateFrustum)
	{
		camera.view = view;
		camera.invView = Mat4::Inverse(camera.view);
		camera.projection = Mat4::Perspective(Math::Radians(fov), outputSize.width / (f32)outputSize.height, nearClip, farClip);
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
			Vec2 jitterOffsets = Vec2{jitterValues.x * 2 - 1.0f, jitterValues.y * 2 - 1.0f};

			static f32 jitterScale = 1.0f;

			jitterOffsets.x *= jitterScale;
			jitterOffsets.y *= jitterScale;

			camera.previousJitter = camera.jitter;
			camera.jitter = Vec2{jitterOffsets.x / static_cast<f32>(outputSize.width), jitterOffsets.y / static_cast<f32>(outputSize.height)};

			Mat4 jitterMattrix = Mat4::Translate(Mat4{1.0}, Vec3{camera.jitter.x, camera.jitter.y, 0.f});
			camera.projection = jitterMattrix * camera.projection;

			camera.previousJitterXy = camera.jitterXy;
			camera.jitterXy = Vec2{camera.jitter.x / outputSize.width, camera.jitter.y / outputSize.height};
		}
		
		camera.viewProjection = camera.projection * camera.view;
		camera.invViewProj = Mat4::Inverse(camera.viewProjection);

		if (updateFrustum)
		{
			camera.frustum = Math::CreateFrustumFromCamera(camera.viewProjection);
		}
	}

	void RenderPipelineContext::SetOutputSize(Extent size)
	{
		if (size != outputSize)
		{
			outputSize = size;
			Resize();
		}
	}

	void RenderPipelineContext::SetTexture(StringView textureName, GPUTexture* texture)
	{
		auto it = resources.Find(textureName);
		if (it == resources.end())
		{
			it = resources.Insert(textureName, PipelineResourceStorage{.desc = {.name = textureName}}).first;
		}
		it->second.textures[0] = texture;
		it->second.ownsResource = false;
	}

	GPUTexture* RenderPipelineContext::GetTexture(StringView textureName)
	{
		if (auto it = resources.Find(textureName))
		{
			return it->second.GetResourceTexture(currentFrame);
		}
		return nullptr;
	}

	GPUTexture* RenderPipelineContext::GetPrevTexture(StringView textureName)
	{
		if (auto it = resources.Find(textureName))
		{
			return it->second.GetTexture(prevFrame);
		}
		return nullptr;
	}

	VoidPtr RenderPipelineContext::GetInstanceData(StringView name)
	{
		if (auto it = resources.Find(name))
		{
			return it->second.instanceData;
		}
		return nullptr;
	}

	GPUBuffer* RenderPipelineContext::GetBuffer(StringView name)
	{
		if (auto it = resources.Find(name))
		{
			return it->second.buffer;
		}
		return nullptr;
	}

	void RenderPipelineContext::SetOutputAttachments(StringView name, Span<GPUTexture*> attachments, ResourceState requiredState)
	{
		logger.Debug("Set output attachments for {}, size {}", name, attachments.Size());

		colorOutputName = name;

		auto it = resources.Find(name);
		if (it == resources.end())
		{
			it = resources.Emplace(name, PipelineResourceStorage{}).first;
		}

		it->second.desc.name = name;
		it->second.desc.type = RenderPipelineResourceType::Attachment;
		it->second.ownsResource = false;
		it->second.outputAttachments = attachments;
		it->second.requiredState = requiredState;
	}

	void RenderPipelineContext::SetOutputViewsPerAttachment(u32 viewCount)
	{
		ViewCount = viewCount;
	}

	void RenderPipelineContext::SetCurrentOutputIndex(u32 index)
	{
		currentOutputIndex = index;
	}

	Array<RenderPipelineContext::PipelineResourceStorage> RenderPipelineContext::GetResources() const
	{
		Array<PipelineResourceStorage> result;
		for (auto& resource : resources)
		{
			result.EmplaceBack(resource.second);
		}
		return result;
	}

	GPUTexture* RenderPipelineContext::GetColorOutput() const
	{
		if (auto it = resources.Find(colorOutputName))
		{
			if (GPUTexture* texture = it->second.GetTexture(currentFrame))
			{
				return texture;
			}

			if (it->second.textureView)
			{
				return it->second.textureView->GetTexture();
			}

			if (it->second.outputAttachments.Size() > 0)
			{
				return it->second.outputAttachments[currentOutputIndex];
			}
		}
		return nullptr;
	}

	GPUTexture* RenderPipelineContext::GetDepthOutput() const
	{
		if (auto it = resources.Find(depthOutputName))
		{
			if (GPUTexture* texture = it->second.GetTexture(currentFrame))
			{
				return texture;
			}
		}
		return nullptr;
	}

	void RenderPipelineContext::SetColorOutput(StringView name)
	{
		colorOutputName = name;
	}

	void RenderPipelineContext::SetDepthOutput(StringView name)
	{
		depthOutputName = name;
	}

	void RenderPipelineContext::DisableContext(bool disabled)
	{
		contextDisabled = disabled;
	}

	TypeID RenderPipelineContext::GetPipelineTypeId() const
	{
		return renderPipeline->GetTypeId();
	}

	void RenderPipelineContext::Execute(GPUCommandBuffer* cmd, Scene* scene)
	{
		logger.Debug("------------  Frame {} ---------------", App::Frame());

		GraphicsAPI api = Graphics::GetDevice()->GetAPI();

		if (scene)
		{
			scene->renderObjects.DoUpdate(cmd);
		}

		if (!contextDisabled)
		{

			auto executeBarriers = [&](const Span<PassBarrier> barriers)
			{
				for (auto& barrier : barriers)
				{
					if (auto it = resources.Find(barrier.resource))
					{

						logger.Debug("Resource {} barrier {} -> {}", barrier.resource, ResourceStateToString(barrier.srcState), ResourceStateToString(barrier.dstState));

						if (GPUTexture* texture = it->second.GetTexture(currentFrame))
						{
							cmd->ResourceBarrier(texture, barrier.srcState, barrier.dstState, 0, 0);
						}
						else if (it->second.textureView)
						{
							cmd->ResourceBarrier(it->second.textureView->GetTexture(), barrier.srcState, barrier.dstState, 0, 0);
						}
						else if (!it->second.outputAttachments.Empty())
						{
							cmd->ResourceBarrier(it->second.outputAttachments[currentOutputIndex], barrier.srcState, barrier.dstState, 0, 0);
						}
					}
				}
			};

			if (resourceDirty)
			{
				executeBarriers(initializationBarriers);
				resourceDirty = false;
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
				.outputSize = IVec2{static_cast<i32>(GetOutputSize().width), static_cast<i32>(GetOutputSize().height)},
				.jitter = camera.jitter,
				.prevJitter = camera.previousJitter,
				.instanceCount = scene->renderObjects.instanceDataCount,
				.cullingMask = camera.cullingMask,
			};

			for (u32 i = 0; i < 6; ++i)
			{
				const Plane& plane = camera.frustum.planes[i];
				sceneBufferData->frustumPlanes[i] = Vec4{plane.normal.x, plane.normal.y, plane.normal.z, plane.distance};
			}

			sceneDescriptorSets[currentFrame]->UpdateBuffer(1, scene->renderObjects.instanceDataBuffer, 0, 0);

			for (auto& module : modules)
			{
				module->Update(scene);
			}

			for (auto& storage : passes)
			{
				storage.pass->Update();
			}

			cmd->BeginDebugMarker(renderPipeline->GetType()->GetSimpleName(), Vec4(0.0, 0.0, 0.0, 1.0));
			SK_SCOPED_ZONE(renderPipeline->GetType()->GetSimpleName(), cmd);

			for (auto& storage : passes)
			{
				SK_SCOPED_ZONE(storage.name, cmd);

				if (storage.setup.type != RenderPipelinePassType::Other)
				{
					cmd->BeginDebugMarker(storage.name, Vec4(0.0, 0.0, 0.0, 1.0));
				}

				executeBarriers(storage.preBarriers);

				if (storage.pass->renderPass)
				{
					ClearValues clearValues = {};
					clearValues.color = Vec4(0.1, 0.1, 0.1, 1.0);

					GPUFramebuffer* framebuffer = storage.GetCurrentFramebuffer(currentOutputIndex);

					BeginRenderPassInfo beginRenderPassInfo;
					beginRenderPassInfo.renderPass = storage.pass->renderPass;
					beginRenderPassInfo.framebuffer = framebuffer;
					beginRenderPassInfo.clearValues = &clearValues;
					cmd->BeginRenderPass(beginRenderPassInfo);

					Extent extent = framebuffer->GetExtent();

					ViewportInfo viewportInfo{};
					viewportInfo.x = 0.;
					viewportInfo.y = 0.;

					if (api == GraphicsAPI::Vulkan && storage.setup.invertViewport)
					{
						viewportInfo.y = (f32)extent.height;
						viewportInfo.width = (f32)extent.width;
						viewportInfo.height = -(f32)extent.height;
					}
					else
					{
						viewportInfo.y = 0.;
						viewportInfo.width = (f32)extent.width;
						viewportInfo.height = (f32)extent.height;
					}

					viewportInfo.minDepth = 0.;
					viewportInfo.maxDepth = 1.;

					cmd->SetViewport(viewportInfo);
					cmd->SetScissor({0, 0}, extent);
				}

				storage.pass->Render(scene, cmd);

				if (storage.pass->renderPass)
				{
					cmd->EndRenderPass();
				}

				executeBarriers(storage.postBarriers);

				if (storage.setup.type != RenderPipelinePassType::Other)
				{
					cmd->EndDebugMarker();
				}
			}

			cmd->EndDebugMarker();
		}
		prevFrame = currentFrame;
		currentFrame = (currentFrame + 1) % SK_FRAMES_IN_FLIGHT;
	}

	void RenderPipelineContext::Destroy()
	{
		Graphics::WaitIdle();

		for (auto& storage : passes)
		{
			if (storage.pass->renderPass)
			{
				storage.pass->renderPass->Destroy();
			}

			for (GPUFramebuffer* frambuffer : storage.framebuffers)
			{
				frambuffer->Destroy();
			}

			storage.pass->Destroy();
			DestroyAndFree(storage.pass);
		}

		for (auto& module : modules)
		{
			module->Destroy();
			DestroyAndFree(module);
		}

		DestroyAndFree(renderPipeline);

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			if (sceneDescriptorSets[i]) sceneDescriptorSets[i]->Destroy();
		}

		if (sceneBuffer) sceneBuffer->Destroy();

		for (auto it : resources)
		{
			it.second.Destroy();
		}

		DestroyAndFree(this);
	}

	GPUDescriptorSet* RenderPipelineContext::GetSceneDescriptorSet() const
	{
		return sceneDescriptorSets[currentFrame];
	}

	GPUDescriptorSet* RenderPipelineContext::GetSceneDescriptorSet(u32 frame) const
	{
		return sceneDescriptorSets[frame];
	}

	GPUFramebuffer* RenderPipelineContext::PassStorage::GetCurrentFramebuffer(u32 index) const
	{
		if (framebuffers.Size() > index)
		{
			return framebuffers[index];
		}

		if (!framebuffers.Empty())
		{
			return framebuffers.Back();
		}

		return nullptr;
	}

	void RenderPipelineContext::PassStorage::CreateFrameBuffers(RenderPipelineContext* context)
	{
		for (GPUFramebuffer* framebuffer : framebuffers)
		{
			framebuffer->Destroy();
		}
		framebuffers.Clear();

		Array<FramebufferDesc> framebufferDescs;

		for (auto& dependency : setup.dependencies)
		{
			if (dependency.access == RenderPipelineTextureAccess::Write || dependency.access == RenderPipelineTextureAccess::ReadWrite)
			{
				if (auto it = context->resources.Find(dependency.name))
				{
					if (it->second.desc.type == RenderPipelineResourceType::Attachment)
					{
						it->second.IterateTextures([&](GPUTexture* texture, u32 index)
						{
							if (framebufferDescs.Size() >= index)
							{
								framebufferDescs.Resize(index + 1);
								framebufferDescs[index].renderPass = pass->renderPass;
							}

							logger.Debug("added to framebuffer {} for {} attachment {} at {} ", index, name, it->second.desc.name, framebufferDescs[index].attachments.Size());
							framebufferDescs[index].attachments.EmplaceBack(texture->GetTextureView());
						});
					}
				}
			}
		}

		for (auto& framebufferDesc : framebufferDescs)
		{
			if (!framebufferDesc.attachments.Empty())
			{
				if (GPUFramebuffer* framebuffer = Graphics::CreateFramebuffer(framebufferDesc))
				{
					framebuffers.EmplaceBack(framebuffer);
				}
			}
		}
	}

	void RenderPipelineContext::PipelineResourceStorage::Destroy()
	{
		if (!ownsResource)
		{
			return;
		}

		for (GPUTexture*& texture : textures)
		{
			if (texture)
			{
				texture->Destroy();
				texture = nullptr;
			}
		}

		if (textureView)
		{
			textureView->Destroy();
			textureView = nullptr;
		}

		if (buffer)
		{
			buffer->Destroy();
			buffer = nullptr;
		}

		if (instanceData)
		{
			if (ReflectType* type = Reflection::FindTypeById(desc.instanceTypeId))
			{
				type->Destructor(instanceData);
			}
			MemFree(instanceData);
			instanceData = nullptr;
		}
	}

	void RenderPipelineContext::CreateContextResources()
	{
		resourceCreated = true;
		resourceDirty = true;

		for (auto& resourceIt : resources)
		{
			auto& storage = resourceIt.second;
			auto& desc = storage.desc;

			switch (desc.type)
			{
				case RenderPipelineResourceType::None:
				case RenderPipelineResourceType::Reference:
					break;
				case RenderPipelineResourceType::Attachment:
				case RenderPipelineResourceType::Texture:
				{
					if (desc.format == TextureFormat::Unknown)
					{
						continue;
					}

					if (storage.HasTexture())
					{
						continue;
					}


					storage.textureDesc.format = desc.format;
					storage.textureDesc.usage = ResourceUsage::ShaderResource;
					storage.textureDesc.mipLevels = desc.mipLevels;
					storage.textureDesc.arrayLayers = desc.arrayLayers;
					storage.textureDesc.sampleCount = desc.samples;

					bool isDepth = IsDepthFormat(storage.textureDesc.format);

					if (desc.type == RenderPipelineResourceType::Attachment)
					{
						storage.textureDesc.usage |= !isDepth ? ResourceUsage::RenderTarget : ResourceUsage::DepthStencil;
					}

					if (desc.textureUsage != ResourceUsage::None)
					{
						storage.textureDesc.usage |= desc.textureUsage;
					}

					if (desc.extent.width > 0 && desc.extent.height > 0)
					{
						storage.textureDesc.extent = desc.extent;
					}
					else if (desc.scale.width > 0 && desc.scale.height > 0)
					{
						storage.textureDesc.extent = outputSize * desc.scale;
						storage.textureUseOutputSize = true;
					}
					else
					{
						logger.Error("Invalid texture extent for resource {} ", resourceIt.first);
						continue;
					}

					for (u32 i = 0; i < (desc.pingPong ? 2 : 1); ++i)
					{
						storage.textures[i] = Graphics::CreateTexture(storage.textureDesc);
					}

					storage.ownsResource = true;
					storage.requiredState = isDepth ? ResourceState::DepthStencilReadOnly : ResourceState::ShaderReadOnly;
					break;
				}
				case RenderPipelineResourceType::Buffer:
				{
					BufferDesc bufferDesc;
					bufferDesc.size = desc.size;
					bufferDesc.usage = desc.usage;
					bufferDesc.hostVisible = desc.hostVisible;
					bufferDesc.persistentMapped = desc.persistentMapped;
					bufferDesc.debugName = "PipelineResource_" + desc.name;

					storage.buffer = Graphics::CreateBuffer(bufferDesc);
					storage.ownsResource = true;
					break;
				}
				case RenderPipelineResourceType::Instance:
				{
					ReflectType* type = Reflection::FindTypeById(desc.instanceTypeId);
					SK_ASSERT(type, "type not found");
					if (type)
					{
						if (ReflectConstructor* constructor = type->GetDefaultConstructor())
						{
							storage.instanceData = MemAlloc(type->GetProps().size);
							storage.instanceTypeId = desc.instanceTypeId;
							storage.ownsResource = true;
							constructor->Construct(storage.instanceData, nullptr);
						}
					}
					break;
				}
				default:
					logger.Warn("resource type for {} not implemented yet", resourceIt.first);
					break;
			}
		}
	}

	void RenderPipelineContext::CreatePasses()
	{
		//create render passes / framebuffers
		for (i32 i = 0; i < passes.Size(); ++i)
		{
			auto& storage = passes[i];
			if (storage.setup.type != RenderPipelinePassType::Graphics)
			{
				continue;
			}

			RenderPassDesc renderPassDesc;

			for (auto& dependency : storage.setup.dependencies)
			{
				if (dependency.access == RenderPipelineTextureAccess::Write || dependency.access == RenderPipelineTextureAccess::ReadWrite)
				{
					if (auto it = resources.Find(dependency.name))
					{
						if (it->second.desc.type == RenderPipelineResourceType::Attachment)
						{
							GPUTexture* texture = it->second.GetResourceTexture(currentFrame);
							if (texture == nullptr)
							{
								logger.Error("Texture {} not created", dependency.name);
								continue;
							}

							const TextureDesc& textureDesc = texture->GetDesc();
							bool isDepth = IsDepthFormat(textureDesc.format);

							AttachmentDesc attachmentDesc;
							attachmentDesc.finalState = !isDepth ? ResourceState::ColorAttachment : ResourceState::DepthStencilAttachment;

							if (dependency.access == RenderPipelineTextureAccess::Write)
							{
								attachmentDesc.initialState = ResourceState::Undefined;
								attachmentDesc.loadOp = AttachmentLoadOp::Clear;
							}
							else
							{
								attachmentDesc.initialState = attachmentDesc.finalState;
								attachmentDesc.loadOp = AttachmentLoadOp::Load;
							}

							attachmentDesc.storeOp = AttachmentStoreOp::Store;
							attachmentDesc.sampleCount = textureDesc.sampleCount;
							attachmentDesc.format = textureDesc.format;

							bool resolve = storage.setup.resolve.IndexOf(dependency.name) != nPos;

							if (it->second.desc.type == RenderPipelineResourceType::Attachment)
							{
								if (!resolve)
								{
									renderPassDesc.attachments.EmplaceBack(attachmentDesc);
								}
								else
								{
									renderPassDesc.resolveAttachments.EmplaceBack(attachmentDesc);
								}
							}
						}
					}
				}
			}

			if (!renderPassDesc.attachments.Empty())
			{
				storage.pass->renderPass = Graphics::CreateRenderPass(renderPassDesc);
				storage.CreateFrameBuffers(this);
				SK_ASSERT(storage.pass->renderPass, "render pass cannot bre created");
			}
			else
			{
				logger.Error("no attachments for pass {}", storage.name);
			}
		}
	}

	void RenderPipelineContext::CreateBarriers()
	{
		for (u32 i = 0; i < passes.Size(); ++i)
		{
			auto& pass = passes[i];

			if (pass.setup.type == RenderPipelinePassType::Other) continue;

			for (auto& dependency : pass.setup.dependencies)
			{
				GPUTexture* texture = GetTexture(dependency.name);
				if (texture == nullptr) continue;
				bool isDepth = IsDepthFormat(texture->GetDesc().format);

				//post barriers
				if (dependency.access == RenderPipelineTextureAccess::Write || dependency.access == RenderPipelineTextureAccess::ReadWrite)
				{
					PassBarrier barrier;
					barrier.resource = dependency.name;

					//src
					if (pass.setup.type == RenderPipelinePassType::Graphics)
					{
						barrier.srcState = isDepth ? ResourceState::DepthStencilAttachment : ResourceState::ColorAttachment;
					}
					else //compute and raytrace
					{
						barrier.srcState = ResourceState::General;
					}

					auto nextUsage = GetNextUsageInfo(i, dependency.name);

					//if it never used make readonly
					if (nextUsage.passIndex == U32_MAX || nextUsage.access == RenderPipelineTextureAccess::Read)
					{
						auto resIt = resources.Find(dependency.name);
						if (resIt != resources.end()
							&& resIt->second.requiredState != ResourceState::Undefined
							&& i == resIt->second.lastWrite)
						{
							barrier.dstState = resIt->second.requiredState;
						}
						else
						{
							barrier.dstState = isDepth ? ResourceState::DepthStencilReadOnly : ResourceState::ShaderReadOnly;
						}
					}
					else if (nextUsage.access == RenderPipelineTextureAccess::ReadWrite || nextUsage.access == RenderPipelineTextureAccess::Write)
					{
						if (passes[nextUsage.passIndex].setup.type == RenderPipelinePassType::Graphics)
						{
							barrier.dstState = isDepth ? ResourceState::DepthStencilAttachment : ResourceState::ColorAttachment;
						}
						else
						{
							barrier.dstState = ResourceState::General;
						}
 					}
					pass.postBarriers.EmplaceBack(barrier);
				}

				//pre barriers
				if (dependency.access == RenderPipelineTextureAccess::Write || dependency.access == RenderPipelineTextureAccess::ReadWrite)
				{
					auto info = GetPreviousUsageInfo(i, dependency.name);

					if (info.access == RenderPipelineTextureAccess::Read)
					{
						PassBarrier barrier;
						barrier.resource = dependency.name;
						barrier.srcState = isDepth ? ResourceState::DepthStencilReadOnly : ResourceState::ShaderReadOnly;

						if (pass.setup.type == RenderPipelinePassType::Graphics)
						{
							barrier.dstState = isDepth ? ResourceState::DepthStencilAttachment : ResourceState::ColorAttachment;
						}
						else
						{
							barrier.dstState = ResourceState::General;
						}

						pass.preBarriers.EmplaceBack(barrier);
					}

					//is it the first write of the frame and not graphics?
					if (info.passIndex == U32_MAX && pass.setup.type != RenderPipelinePassType::Graphics)
					{
						auto lastUsage = GetLastUsageInfo(dependency.name);

						PassBarrier barrier;
						barrier.resource = dependency.name;

						auto resIt = resources.Find(dependency.name);
						bool isExternal = resIt != resources.end() && !resIt->second.outputAttachments.Empty();

						if (isExternal && dependency.access == RenderPipelineTextureAccess::Write)
						{
							barrier.srcState = ResourceState::Undefined;
						}
						else if ((lastUsage.access == RenderPipelineTextureAccess::Write || lastUsage.access == RenderPipelineTextureAccess::ReadWrite) && passes[lastUsage.passIndex].setup.type != RenderPipelinePassType::Graphics)
						{
							barrier.srcState = ResourceState::General;
						}
						else
						{
							barrier.srcState = isDepth ? ResourceState::DepthStencilReadOnly : ResourceState::ShaderReadOnly;
						}

						barrier.dstState = ResourceState::General;
						pass.preBarriers.EmplaceBack(barrier);
					}
				}
			}
		}

		for (auto& resIt : resources)
		{
			if (resIt.second.desc.type == RenderPipelineResourceType::Attachment || resIt.second.desc.type == RenderPipelineResourceType::Texture)
			{
				if (!resIt.second.outputAttachments.Empty()) continue;

				auto firstUsage = GetFirstUsageInfo(resIt.second.desc.name);

				if (firstUsage.passIndex != U32_MAX && passes[firstUsage.passIndex].setup.type != RenderPipelinePassType::Graphics)
				{
					auto lastUsage = GetLastUsageInfo(resIt.second.desc.name);

					bool isDepth = IsDepthFormat(resIt.second.desc.format);

					PassBarrier barrier;
					barrier.resource = resIt.second.desc.name;
					barrier.srcState = ResourceState::Undefined;
					barrier.dstState = lastUsage.access == RenderPipelineTextureAccess::Write ? ResourceState::General : !isDepth ? ResourceState::ShaderReadOnly : ResourceState::DepthStencilReadOnly;
					initializationBarriers.EmplaceBack(barrier);
				}
			}
		}

	}

	void RenderPipelineContext::Resize()
	{
		if (!resourceCreated) return;
		resourceDirty = true;

		Graphics::WaitIdle();

		for (auto& resourceIt : resources)
		{
			auto& storage = resourceIt.second;
			auto& desc = storage.desc;
			if (storage.textureUseOutputSize)
			{
				for (GPUTexture*& texture : storage.textures)
				{
					if (texture)
					{
						texture->Destroy();
						texture = nullptr;
					}
				}
				storage.textureDesc.extent = outputSize * desc.scale;

				for (u32 i = 0; i < (desc.pingPong ? 2 : 1); ++i)
				{
					storage.textures[i] = Graphics::CreateTexture(storage.textureDesc);
				}
			}
		}

		for (auto& pass : passes)
		{
			if (pass.setup.type == RenderPipelinePassType::Graphics)
			{
				pass.CreateFrameBuffers(this);
			}
		}

		for (auto& storage : passes)
		{
			storage.pass->OnResize(outputSize);
		}
	}

	RenderPipelineContext::ResourceUsageInfo RenderPipelineContext::GetNextUsageInfo(u32 currentPass, StringView attachmentName) const
	{
		if (currentPass >= passes.Size() - 1) return ResourceUsageInfo{};

		//check if the next passes contain a writing for this attachment.
		for (u32 i = currentPass + 1; i < passes.Size(); ++i)
		{
			auto& pass = passes[i];
			for (auto& dependency : pass.setup.dependencies)
			{
				if (dependency.name == attachmentName)
				{
					return {i, dependency.access};
				}
			}
		}
		return ResourceUsageInfo{};
	}

	RenderPipelineContext::ResourceUsageInfo RenderPipelineContext::GetPreviousUsageInfo(u32 currentPass, StringView attachmentName) const
	{
		if (currentPass == 0) return ResourceUsageInfo{};
		if (currentPass > passes.Size()) return ResourceUsageInfo{};

		for (i32 i = static_cast<i32>(currentPass) - 1; i >= 0; --i)
		{
			auto& pass = passes[i];
			for (auto& dependency : pass.setup.dependencies)
			{
				if (dependency.name == attachmentName)
				{
					return {static_cast<u32>(i), dependency.access};
				}
			}
		}

		return ResourceUsageInfo{};
	}

	RenderPipelineContext::ResourceUsageInfo RenderPipelineContext::GetLastUsageInfo(StringView attachmentName) const
	{
		return GetPreviousUsageInfo(passes.Size(), attachmentName);
	}

	RenderPipelineContext::ResourceUsageInfo RenderPipelineContext::GetFirstUsageInfo(StringView attachmentName) const
	{
		return GetNextUsageInfo(0, attachmentName);
	}


	RenderPipelineContext* RenderPipeline::CreateContext(TypeID pipelineTypeId, Span<TypeID> extraModules, const RenderPipelineContextSettings& settings)
	{
		ReflectType* pipelineReflectType = Reflection::FindTypeById(pipelineTypeId);
		if (!pipelineReflectType)
		{
			logger.Error("Could not find pipeline type with id {}", pipelineTypeId);
			return nullptr;
		}

		Object* object = pipelineReflectType->NewObject();
		if (!object)
		{
			logger.Error("Could not create pipeline object for type {} ", pipelineReflectType->GetName());
			return nullptr;
		}

		RenderPipeline* renderPipeline = object->SafeCast<RenderPipeline>();
		if (!renderPipeline)
		{
			logger.Error("Could not cast object to RenderPipeline");
			DestroyAndFree(object);
			return nullptr;
		}

		RenderPipelineContext* context = Alloc<RenderPipelineContext>(renderPipeline, extraModules, settings);

		logger.Debug("Successfully created render pipeline context for {}", pipelineReflectType->GetName());
		return context;
	}

	static RenderPipelineContext* mainContext = nullptr;

	RenderPipelineContext* RenderPipeline::GetMainContext()
	{
		return mainContext;
	}

	void RenderPipeline::SetMainContext(RenderPipelineContext* context)
	{
		mainContext = context;
	}
}