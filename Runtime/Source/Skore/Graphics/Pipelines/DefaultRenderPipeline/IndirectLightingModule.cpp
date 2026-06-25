#include <cmath>
#include <cstring>
#include <variant>

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Components/IrradianceVolumeComponent.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Profiler.hpp"

namespace Skore
{
	constexpr u32 MaxIrradianceVolumes = 16;
	constexpr i32 IrradianceRelocationEnabledFlag = 1 << 0;
	constexpr i32 IrradianceClassificationEnabledFlag = 1 << 1;

	struct IrradianceVolumeGPU
	{
		Vec4 origin;
		Vec4 probeSpacing;
		Vec4 probeRayRotation;

		i32 probeCountsX, probeCountsY, probeCountsZ, flags;
		i32 scrollOffsetsX, scrollOffsetsY, scrollOffsetsZ, scrollPad;
		i32 scrollDeltaX, scrollDeltaY, scrollDeltaZ, scrollClearAll;

		i32 irradianceTextureWidth, irradianceTextureHeight, irradianceSideLength, raysPerProbe;
		i32 visibilityTextureWidth, visibilityTextureHeight, visibilitySideLength, frame;

		f32 hysteresis, normalBias, viewBias, maxRayDistance;
		f32 irradianceGamma, energyConservation, distanceExponent, pad0;
	};

	struct IrradianceVolume
	{
		f32 spacingX = 1.0f, spacingY = 1.0f, spacingZ = 1.0f;
		bool isCascade = true;

		i32 cornerX = 0, cornerY = 0, cornerZ = 0;
		i32 scrollX = 0, scrollY = 0, scrollZ = 0;
		Vec4 rayRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
		i32 updateCount = 0;
	};

	struct IrradianceVolumeData
	{
		GPUBuffer*  volumeBuffer = nullptr;
		u32         volumeCount = 0;
		u32         probesPerVolume = 0;
		GPUTexture* irradianceArray = nullptr;
		GPUTexture* distanceArray = nullptr;
		GPUTexture* probeDataArray = nullptr;
		f32         indirectIntensity = 1.0f;

		static void RegisterType(NativeReflectType<IrradianceVolumeData>&) {}
	};

	struct IrradianceTracePushConstants
	{
		u32  volumeIndex;
		u32  enableMultibounce;
		u32  volumeCount;
		u32  ambientMode;
		Vec3 ambientColor;
		f32  ambientIntensity;
	};

	struct IrradianceBlendPushConstants
	{
		u32 volumeIndex;
		u32 volumeCount;
		u32 pad1, pad2;
	};

	struct IrradianceSamplePushConstants
	{
		u32 volumeCount;
		f32 indirectIntensity;
		u32 pad0, pad1;
	};

	struct IrradianceDebugPushConstants
	{
		u32 vertexByteOffset;
		u32 vertexLayoutIndex;
		u32 volumeCount;
		u32 renderVolumeIndex;
	};

	static i32 PositiveMod(i32 a, i32 n)
	{
		return ((a % n) + n) % n;
	}

	static String CascadeProbeOptionName(u32 index)
	{
		return "Show Probe Cascade " + ToString(static_cast<u64>(index));
	}

	//irradiance passes only run when the scene actually has an IrradianceVolumeComponent;
	//checked from IsEnabled() so the render graph drops them when the component is absent.
	static bool SceneHasIrradianceVolume(RenderPipelineContext* context)
	{
		Scene* scene = context->GetScene();
		return scene != nullptr && scene->HasIterable<IrradianceVolumeComponent>();
	}

	struct IrradianceVolumeUpdatePass : RenderPipelinePass
	{
		SK_CLASS(IrradianceVolumeUpdatePass, RenderPipelinePass);

		IrradianceVolumeData*   data = nullptr;
		GPUPipeline*            tracePipeline = nullptr;
		GPUPipeline*            blendIrrPipeline = nullptr;
		GPUPipeline*            blendDistPipeline = nullptr;
		GPUPipeline*            relocatePipeline = nullptr;
		GPUPipeline*            classifyPipeline = nullptr;
		GPUBuffer*              volumeBuffer = nullptr;
		GPUTexture*             rayDataArray = nullptr;
		GPUTexture*             irradianceArray = nullptr;
		GPUTexture*             distanceArray = nullptr;
		GPUTexture*             probeDataArray = nullptr;
		GPUDescriptorSet*       traceSet[SK_FRAMES_IN_FLIGHT] = {};
		GPUDescriptorSet*       blendIrrSet[SK_FRAMES_IN_FLIGHT] = {};
		GPUDescriptorSet*       blendDistSet[SK_FRAMES_IN_FLIGHT] = {};
		GPUDescriptorSet*       relocateSet[SK_FRAMES_IN_FLIGHT] = {};
		GPUDescriptorSet*       classifySet[SK_FRAMES_IN_FLIGHT] = {};

		Array<IrradianceVolume> volumes;
		Array<i32>              scheduled;
		u32                     nextVolume = 0;
		bool                    texturesInitialized = false;
		u64                     builtConfigVersion = 0;
		u32                     rngState = 2463534242u;
		usize                   stride = sizeof(IrradianceVolumeGPU);

		i32 countX = 16, countY = 8, countZ = 16;
		f32 baseSpacing = 1.0f;
		i32 raysPerProbe = 144;
		i32 irradianceSide = 6;
		i32 visibilitySide = 6;
		i32 irrW = 0, irrH = 0, visW = 0, visH = 0, totalProbes = 0;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Raytrace;
			setup.stage = PipelineRenderStage::Indirect + 1;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		bool IsEnabled() override
		{
			return SceneHasIrradianceVolume(context);
		}

		f32 NextRandom()
		{
			rngState ^= rngState << 13;
			rngState ^= rngState >> 17;
			rngState ^= rngState << 5;
			return f32(rngState & 0xFFFFFFu) / f32(0x1000000);
		}

		Vec4 RandomQuaternion()
		{
			f32 u1 = NextRandom();
			f32 u2 = NextRandom();
			f32 u3 = NextRandom();
			f32 s1 = std::sqrt(1.0f - u1);
			f32 s2 = std::sqrt(u1);
			f32 t2 = 6.28318530718f * u2;
			f32 t3 = 6.28318530718f * u3;
			return Vec4(s1 * std::sin(t2), s1 * std::cos(t2), s2 * std::sin(t3), s2 * std::cos(t3));
		}

		void Init() override
		{
			data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");

			RID shader = Resources::FindByPath("Skore://Shaders/DefaultIndirectLighting.shader");

			tracePipeline = Graphics::CreateRayTracingPipeline(RayTracingPipelineDesc{
				.shader = shader,
				.variant = "IrradianceProbeTrace",
				.maxRecursionDepth = 1,
				.descriptorSetsOverride = {
					DescriptorSetOverride{.set = 0, .descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()},
					DescriptorSetOverride{.set = 1, .descriptorSet = context->GetSceneDescriptorSet(0)}
				}
			});

			blendIrrPipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{.shader = shader, .variant = "IrradianceProbeBlendIrradiance"});
			blendDistPipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{.shader = shader, .variant = "IrradianceProbeBlendDistance"});
			relocatePipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{.shader = shader, .variant = "IrradianceProbeRelocate"});
			classifyPipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{.shader = shader, .variant = "IrradianceProbeClassify"});

			BufferDesc bd;
			bd.size = static_cast<usize>(SK_FRAMES_IN_FLIGHT) * MaxIrradianceVolumes * stride;
			bd.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess;
			bd.hostVisible = true;
			bd.persistentMapped = true;
			bd.debugName = "IrradianceVolumeBuffer";
			volumeBuffer = Graphics::CreateBuffer(bd);

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				traceSet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceProbeTrace", 2);
				blendIrrSet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceProbeBlendIrradiance", 0);
				blendDistSet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceProbeBlendDistance", 0);
				relocateSet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceProbeRelocate", 0);
				classifySet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceProbeClassify", 0);
			}
		}

		void EnsureResources(i32 rays, i32 cascades, i32 probeCountX, i32 probeCountY, i32 probeCountZ, f32 probeSpacing, u64 configVersion)
		{
			if (rayDataArray && configVersion == builtConfigVersion)
			{
				return;
			}
			builtConfigVersion = configVersion;

			if (rayDataArray)
			{
				rayDataArray->Destroy();
				irradianceArray->Destroy();
				distanceArray->Destroy();
				probeDataArray->Destroy();
				rayDataArray = nullptr;
				irradianceArray = nullptr;
				distanceArray = nullptr;
				probeDataArray = nullptr;
			}

			raysPerProbe = rays;
			countX = probeCountX;
			countY = probeCountY;
			countZ = probeCountZ;
			baseSpacing = probeSpacing;

			volumes.Clear();
			f32 spacing = baseSpacing;
			for (i32 c = 0; c < cascades; ++c)
			{
				IrradianceVolume v;
				v.spacingX = spacing;
				v.spacingY = spacing;
				v.spacingZ = spacing;
				v.isCascade = true;
				volumes.EmplaceBack(v);
				spacing *= 2.0f;
			}
			nextVolume = 0;

			i32 irrBorder = irradianceSide + 2;
			i32 visBorder = visibilitySide + 2;
			irrW = irrBorder * countX * countY;
			irrH = irrBorder * countZ;
			visW = visBorder * countX * countY;
			visH = visBorder * countZ;
			totalProbes = countX * countY * countZ;

			u32 layers = static_cast<u32>(volumes.Size());

			rayDataArray = Graphics::CreateTexture(TextureDesc{
				.extent = {static_cast<u32>(raysPerProbe), static_cast<u32>(totalProbes) * layers, 1},
				.format = Format::RGBA16_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
				.debugName = "IrradianceVolumeRayData"
			});
			irradianceArray = Graphics::CreateTexture(TextureDesc{
				.extent = {static_cast<u32>(irrW), static_cast<u32>(irrH) * layers, 1},
				.format = Format::RGBA16_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
				.debugName = "IrradianceVolumeIrradiance"
			});
			distanceArray = Graphics::CreateTexture(TextureDesc{
				.extent = {static_cast<u32>(visW), static_cast<u32>(visH) * layers, 1},
				.format = Format::RG16_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
				.debugName = "IrradianceVolumeDistance"
			});
			probeDataArray = Graphics::CreateTexture(TextureDesc{
				.extent = {static_cast<u32>(countX * countY), static_cast<u32>(countZ) * layers, 1},
				.format = Format::RGBA16_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest,
				.debugName = "IrradianceVolumeProbeData"
			});

			texturesInitialized = false;
		}

		void PublishVolumeData()
		{
			if (!data) data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");
			if (!data) return;
			data->volumeBuffer = volumeBuffer;
			data->volumeCount = static_cast<u32>(volumes.Size());
			data->probesPerVolume = static_cast<u32>(totalProbes);
			data->irradianceArray = irradianceArray;
			data->distanceArray = distanceArray;
			data->probeDataArray = probeDataArray;
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!data) data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");

			IrradianceVolumeComponent* volumeComponent = nullptr;
			if (scene)
			{
				scene->Iterate<IrradianceVolumeComponent>([&](IrradianceVolumeComponent* component)
				{
					if (!volumeComponent) volumeComponent = component;
				});
			}

			if (!scene || scene->renderObjects.tlas == nullptr || volumeComponent == nullptr)
			{
				if (data) data->volumeCount = 0;
				if (LightPassInstanceData* light = context->GetInstanceData<LightPassInstanceData>("LightInstanceData"))
				{
					light->indirectLightFlags &= ~LightFlags::GlobalIlluminationEnabled;
				}
				return;
			}

			i32  desiredRays = Math::Clamp(volumeComponent->GetRaysPerProbe(), 1, 1024);
			i32  desiredCascades = Math::Clamp(volumeComponent->GetCascadeCount(), 1, static_cast<i32>(MaxIrradianceVolumes));
			i32  desiredCountX = Math::Clamp(volumeComponent->GetProbeCountX(), 1, 128);
			i32  desiredCountY = Math::Clamp(volumeComponent->GetProbeCountY(), 1, 128);
			i32  desiredCountZ = Math::Clamp(volumeComponent->GetProbeCountZ(), 1, 128);
			f32  desiredSpacing = Math::Max(volumeComponent->GetProbeSpacing(), 0.01f);
			bool scrollWithCamera = volumeComponent->GetScrollWithCamera();
			f32  hysteresis = Math::Clamp(volumeComponent->GetHysteresis(), 0.0f, 1.0f);
			bool relocationEnabled = volumeComponent->GetProbeRelocation();
			bool classificationEnabled = volumeComponent->GetProbeClassification();

			EnsureResources(desiredRays, desiredCascades, desiredCountX, desiredCountY, desiredCountZ, desiredSpacing, volumeComponent->GetResourceConfigVersion());

			PublishVolumeData();
			if (data) data->indirectIntensity = volumeComponent->GetIndirectIntensity();

			u32 frame = context->GetCurrentFrame();
			u32 layers = static_cast<u32>(volumes.Size());
			LightPassInstanceData* light = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			if (!texturesInitialized)
			{
				cmd->ResourceBarrier(TextureBarrierDesc{.texture = rayDataArray, .oldState = ResourceState::Undefined, .newState = ResourceState::General});
				cmd->ResourceBarrier(TextureBarrierDesc{.texture = irradianceArray, .oldState = ResourceState::Undefined, .newState = ResourceState::ShaderReadOnly});
				cmd->ResourceBarrier(TextureBarrierDesc{.texture = distanceArray, .oldState = ResourceState::Undefined, .newState = ResourceState::ShaderReadOnly});
				cmd->ResourceBarrier(TextureBarrierDesc{.texture = probeDataArray, .oldState = ResourceState::Undefined, .newState = ResourceState::CopyDest});
				cmd->ClearColorTexture(probeDataArray, Vec4(0.0f, 0.0f, 0.0f, 0.0f), 0, 0);
				cmd->ResourceBarrier(TextureBarrierDesc{.texture = probeDataArray, .oldState = ResourceState::CopyDest, .newState = ResourceState::General});
				texturesInitialized = true;
			}

			scheduled.Clear();
			scheduled.EmplaceBack(static_cast<i32>(nextVolume));
			nextVolume = (nextVolume + 1) % layers;

			i32  volumeFlags = (relocationEnabled ? IrradianceRelocationEnabledFlag : 0) | (classificationEnabled ? IrradianceClassificationEnabledFlag : 0);

			Vec3 cam = scrollWithCamera ? context->camera.cameraPosition : volumeComponent->GetEntity()->GetWorldPosition();

			IrradianceVolumeGPU gpuArray[MaxIrradianceVolumes];
			for (usize i = 0; i < volumes.Size(); ++i)
			{
				IrradianceVolume& v = volumes[i];

				bool isScheduled = false;
				for (i32 s : scheduled) { if (s == static_cast<i32>(i)) isScheduled = true; }

				i32 dx = 0, dy = 0, dz = 0;
				bool clearAll = false;

				if (isScheduled)
				{
					i32 ncx = static_cast<i32>(std::floor(cam.x / v.spacingX)) - countX / 2;
					i32 ncy = static_cast<i32>(std::floor(cam.y / v.spacingY)) - countY / 2;
					i32 ncz = static_cast<i32>(std::floor(cam.z / v.spacingZ)) - countZ / 2;

					dx = ncx - v.cornerX;
					dy = ncy - v.cornerY;
					dz = ncz - v.cornerZ;

					if (Math::Abs(dx) >= countX || Math::Abs(dy) >= countY || Math::Abs(dz) >= countZ)
					{
						clearAll = true;
					}

					dx = Math::Clamp(dx, -countX, countX);
					dy = Math::Clamp(dy, -countY, countY);
					dz = Math::Clamp(dz, -countZ, countZ);

					v.cornerX = ncx;
					v.cornerY = ncy;
					v.cornerZ = ncz;
					v.scrollX = PositiveMod(ncx, countX);
					v.scrollY = PositiveMod(ncy, countY);
					v.scrollZ = PositiveMod(ncz, countZ);
					v.rayRotation = RandomQuaternion();
					v.updateCount++;
				}

				IrradianceVolumeGPU& g = gpuArray[i];
				g.origin = Vec4(v.cornerX * v.spacingX, v.cornerY * v.spacingY, v.cornerZ * v.spacingZ, 0.0f);
				g.probeSpacing = Vec4(v.spacingX, v.spacingY, v.spacingZ, 0.0f);
				g.probeRayRotation = v.rayRotation;
				g.probeCountsX = countX;
				g.probeCountsY = countY;
				g.probeCountsZ = countZ;
				g.flags = volumeFlags;
				g.scrollOffsetsX = v.scrollX;
				g.scrollOffsetsY = v.scrollY;
				g.scrollOffsetsZ = v.scrollZ;
				g.scrollPad = 0;
				g.scrollDeltaX = dx;
				g.scrollDeltaY = dy;
				g.scrollDeltaZ = dz;
				g.scrollClearAll = clearAll ? 1 : 0;
				g.irradianceTextureWidth = irrW;
				g.irradianceTextureHeight = irrH;
				g.irradianceSideLength = irradianceSide;
				g.raysPerProbe = raysPerProbe;
				g.visibilityTextureWidth = visW;
				g.visibilityTextureHeight = visH;
				g.visibilitySideLength = visibilitySide;
				g.frame = v.updateCount;
				g.hysteresis = hysteresis;
				g.normalBias = v.spacingX * 0.2f;
				g.viewBias = v.spacingX * 0.4f;
				g.maxRayDistance = std::sqrt(v.spacingX * v.spacingX + v.spacingY * v.spacingY + v.spacingZ * v.spacingZ) * 1.5f;
				g.irradianceGamma = 5.0f;
				g.energyConservation = 0.95f;
				g.distanceExponent = 50.0f;
				g.pad0 = 0.0f;
			}

			u8* base = static_cast<u8*>(volumeBuffer->GetMappedData());
			u64 frameOffset = static_cast<u64>(frame) * MaxIrradianceVolumes * stride;
			std::memcpy(base + frameOffset, gpuArray, volumes.Size() * stride);
			u64 range = static_cast<u64>(MaxIrradianceVolumes) * stride;

			GPUSampler* samp = Graphics::GetLinearClampToEdgeSampler();

			GPUDescriptorSet* tset = traceSet[frame];
			tset->UpdateTexture(0, rayDataArray);
			tset->UpdateTexture(1, irradianceArray);
			tset->UpdateTexture(2, distanceArray);
			tset->UpdateTexture(3, light->cubeMapSkyTexture);
			tset->UpdateSampler(4, samp);
			tset->UpdateBuffer(5, volumeBuffer, frameOffset, range);
			tset->UpdateTexture(6, probeDataArray);

			cmd->BeginDebugMarker("IrradianceVolumeTrace", Vec4(0.2f, 0.6f, 1.0f, 1.0f));
			cmd->BindPipeline(tracePipeline);
			cmd->BindDescriptorSet(tracePipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
			cmd->BindDescriptorSet(tracePipeline, 1, context->GetSceneDescriptorSet());
			cmd->BindDescriptorSet(tracePipeline, 2, tset);
			u32 ambientMode = 0;
			if (light->indirectLightFlags & LightFlags::HasAmbientTexture) ambientMode = 1;
			else if (light->indirectLightFlags & LightFlags::HasAmbientColor) ambientMode = 2;

			for (i32 idx : scheduled)
			{
				SK_SCOPED_GPU_ZONE("IrradianceProbeTrace", cmd);
				IrradianceTracePushConstants pc{static_cast<u32>(idx), 1u, layers, ambientMode, light->ambientLight, light->ambientMultiplier};
				cmd->PushConstants(tracePipeline, ShaderStage::RayGen | ShaderStage::ClosestHit | ShaderStage::Miss, 0, sizeof(pc), &pc);
				cmd->TraceRays(tracePipeline, static_cast<u32>(raysPerProbe), static_cast<u32>(totalProbes), 1);
			}
			cmd->EndDebugMarker();

			cmd->MemoryBarrier();

			cmd->BeginDebugMarker("IrradianceVolumeBlend", Vec4(0.6f, 0.2f, 1.0f, 1.0f));
			cmd->ResourceBarrier(TextureBarrierDesc{.texture = irradianceArray, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::General});
			cmd->ResourceBarrier(TextureBarrierDesc{.texture = distanceArray, .oldState = ResourceState::ShaderReadOnly, .newState = ResourceState::General});

			GPUDescriptorSet* bi = blendIrrSet[frame];
			bi->UpdateBuffer(0, volumeBuffer, frameOffset, range);
			bi->UpdateTexture(1, rayDataArray);
			bi->UpdateTexture(2, irradianceArray);
			bi->UpdateTexture(3, probeDataArray);

			GPUDescriptorSet* bd = blendDistSet[frame];
			bd->UpdateBuffer(0, volumeBuffer, frameOffset, range);
			bd->UpdateTexture(1, rayDataArray);
			bd->UpdateTexture(2, distanceArray);
			bd->UpdateTexture(3, probeDataArray);

			for (i32 idx : scheduled)
			{
				IrradianceBlendPushConstants bpc{static_cast<u32>(idx), layers, 0, 0};

				{
					SK_SCOPED_GPU_ZONE("IrradianceProbeBlendIrradiance", cmd);
					cmd->BindPipeline(blendIrrPipeline);
					cmd->BindDescriptorSet(blendIrrPipeline, 0, bi);
					cmd->PushConstants(blendIrrPipeline, ShaderStage::Compute, 0, sizeof(bpc), &bpc);
					cmd->Dispatch((irrW + 7) / 8, (irrH + 7) / 8, 1);
				}

				{
					SK_SCOPED_GPU_ZONE("IrradianceProbeBlendDistance", cmd);
					cmd->BindPipeline(blendDistPipeline);
					cmd->BindDescriptorSet(blendDistPipeline, 0, bd);
					cmd->PushConstants(blendDistPipeline, ShaderStage::Compute, 0, sizeof(bpc), &bpc);
					cmd->Dispatch((visW + 7) / 8, (visH + 7) / 8, 1);
				}
			}

			cmd->ResourceBarrier(TextureBarrierDesc{.texture = irradianceArray, .oldState = ResourceState::General, .newState = ResourceState::ShaderReadOnly});
			cmd->ResourceBarrier(TextureBarrierDesc{.texture = distanceArray, .oldState = ResourceState::General, .newState = ResourceState::ShaderReadOnly});
			cmd->EndDebugMarker();

			if (relocationEnabled || classificationEnabled)
			{
				cmd->MemoryBarrier();
				cmd->BeginDebugMarker("IrradianceVolumeProbeData", Vec4(1.0f, 0.7f, 0.2f, 1.0f));

				GPUDescriptorSet* rs = relocateSet[frame];
				rs->UpdateBuffer(0, volumeBuffer, frameOffset, range);
				rs->UpdateTexture(1, rayDataArray);
				rs->UpdateTexture(2, probeDataArray);

				GPUDescriptorSet* cs = classifySet[frame];
				cs->UpdateBuffer(0, volumeBuffer, frameOffset, range);
				cs->UpdateTexture(1, rayDataArray);
				cs->UpdateTexture(2, probeDataArray);

				u32 groups = (static_cast<u32>(totalProbes) + 63) / 64;

				for (i32 idx : scheduled)
				{
					IrradianceBlendPushConstants ppc{static_cast<u32>(idx), layers, 0, 0};

					if (relocationEnabled)
					{
						SK_SCOPED_GPU_ZONE("IrradianceProbeRelocate", cmd);
						cmd->BindPipeline(relocatePipeline);
						cmd->BindDescriptorSet(relocatePipeline, 0, rs);
						cmd->PushConstants(relocatePipeline, ShaderStage::Compute, 0, sizeof(ppc), &ppc);
						cmd->Dispatch(groups, 1, 1);
					}

					if (relocationEnabled && classificationEnabled)
					{
						cmd->MemoryBarrier();
					}

					if (classificationEnabled)
					{
						SK_SCOPED_GPU_ZONE("IrradianceProbeClassify", cmd);
						cmd->BindPipeline(classifyPipeline);
						cmd->BindDescriptorSet(classifyPipeline, 0, cs);
						cmd->PushConstants(classifyPipeline, ShaderStage::Compute, 0, sizeof(ppc), &ppc);
						cmd->Dispatch(groups, 1, 1);
					}
				}

				cmd->EndDebugMarker();
			}

			cmd->MemoryBarrier();
		}

		void Destroy() override
		{
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				if (traceSet[f]) traceSet[f]->Destroy();
				if (blendIrrSet[f]) blendIrrSet[f]->Destroy();
				if (blendDistSet[f]) blendDistSet[f]->Destroy();
				if (relocateSet[f]) relocateSet[f]->Destroy();
				if (classifySet[f]) classifySet[f]->Destroy();
				traceSet[f] = blendIrrSet[f] = blendDistSet[f] = relocateSet[f] = classifySet[f] = nullptr;
			}
			if (rayDataArray) rayDataArray->Destroy();
			if (irradianceArray) irradianceArray->Destroy();
			if (distanceArray) distanceArray->Destroy();
			if (probeDataArray) probeDataArray->Destroy();
			if (volumeBuffer) volumeBuffer->Destroy();
			if (tracePipeline) tracePipeline->Destroy();
			if (blendIrrPipeline) blendIrrPipeline->Destroy();
			if (blendDistPipeline) blendDistPipeline->Destroy();
			if (relocatePipeline) relocatePipeline->Destroy();
			if (classifyPipeline) classifyPipeline->Destroy();

			//null everything so a re-enable (Init/EnsureResources) rebuilds from a clean state
			//instead of reusing dangling handles - EnsureResources keys off rayDataArray being null.
			rayDataArray = irradianceArray = distanceArray = probeDataArray = nullptr;
			volumeBuffer = nullptr;
			tracePipeline = blendIrrPipeline = blendDistPipeline = relocatePipeline = classifyPipeline = nullptr;
			texturesInitialized = false;
		}
	};


	struct IrradianceVolumeSamplingPass : RenderPipelinePass
	{
		SK_CLASS(IrradianceVolumeSamplingPass, RenderPipelinePass);

		IrradianceVolumeData* data = nullptr;
		GPUPipeline*          pipeline = nullptr;
		GPUDescriptorSet*     descriptorSet[SK_FRAMES_IN_FLIGHT] = {};
		usize                 stride = sizeof(IrradianceVolumeGPU);

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::Indirect + 2;

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "IrradianceVolumeAttachment", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		bool IsEnabled() override
		{
			return SceneHasIrradianceVolume(context);
		}

		void Init() override
		{
			data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");

			RID shader = Resources::FindByPath("Skore://Shaders/DefaultIndirectLighting.shader");

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = shader,
				.variant = "IrradianceVolumeSample",
				.descriptorSetsOverride = {
					DescriptorSetOverride{.set = 1, .descriptorSet = context->GetSceneDescriptorSet(0)}
				}
			});

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				descriptorSet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceVolumeSample", 2);
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene || scene->renderObjects.tlas == nullptr) return;

			data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");
			if (!data || data->volumeBuffer == nullptr || data->volumeCount == 0 || data->irradianceArray == nullptr || data->probeDataArray == nullptr) return;

			u32 frame = context->GetCurrentFrame();
			u64 frameOffset = static_cast<u64>(frame) * MaxIrradianceVolumes * stride;
			u64 range = static_cast<u64>(MaxIrradianceVolumes) * stride;

			if (LightPassInstanceData* light = context->GetInstanceData<LightPassInstanceData>("LightInstanceData"))
			{
				light->indirectLightFlags |= LightFlags::GlobalIlluminationEnabled;
			}

			GPUTexture* output = context->GetTexture("IrradianceVolumeAttachment");

			GPUDescriptorSet* set = descriptorSet[frame];

			set->UpdateTexture(0, output);
			set->UpdateBuffer(1, data->volumeBuffer, frameOffset, range);
			set->UpdateTexture(2, data->irradianceArray);
			set->UpdateTexture(3, data->distanceArray);
			set->UpdateTexture(4, context->GetTexture("GBufferAlbedoMetallic"));
			set->UpdateTexture(5, context->GetTexture("GBufferNormals"));
			set->UpdateTexture(6, context->GetTexture(LinearDepthMipChainName));
			set->UpdateSampler(7, Graphics::GetLinearClampToEdgeSampler());
			set->UpdateSampler(8, Graphics::GetNearestClampToEdgeSampler());
			set->UpdateTexture(9, data->probeDataArray);

			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
			cmd->BindDescriptorSet(pipeline, 2, set);

			f32 intensity = data->indirectIntensity;

			IrradianceSamplePushConstants pc{data->volumeCount, intensity, 0, 0};
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(pc), &pc);

			Extent3D outputExtent = output->GetDesc().extent;
			cmd->Dispatch((outputExtent.width + 7) / 8, (outputExtent.height + 7) / 8, 1);
		}

		void Destroy() override
		{
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				if (descriptorSet[f]) descriptorSet[f]->Destroy();
			}
			if (pipeline) pipeline->Destroy();
		}
	};

	struct IrradianceVolumeDebugPass : RenderPipelinePass
	{
		SK_CLASS(IrradianceVolumeDebugPass, RenderPipelinePass);

		IrradianceVolumeData* data = nullptr;
		GPUPipeline*          pipeline = nullptr;
		GPUDescriptorSet*     descriptorSet[SK_FRAMES_IN_FLIGHT] = {};
		MeshResourceCachePtr  sphereMesh;
		usize                 stride = sizeof(IrradianceVolumeGPU);

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::Indirect + 3;
			setup.invertViewport = true;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::ReadWrite});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		bool IsEnabled() override
		{
			return SceneHasIrradianceVolume(context);
		}

		void Init() override
		{
			data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");

			RID shader = Resources::FindByPath("Skore://Shaders/DefaultIndirectLighting.shader");

			DepthStencilStateDesc depthStencilState;
			depthStencilState.depthTestEnable = true;
			depthStencilState.depthWriteEnable = true;
			depthStencilState.depthCompareOp = CompareOp::GreaterEqual; // reverse-Z

			pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = shader,
				.variant = "IrradianceProbeDebug",
				.rasterizerState = {.cullMode = CullMode::None},
				.depthStencilState = depthStencilState,
				.blendStates = {BlendStateDesc{}},
				.renderPass = renderPass,
				.descriptorSetsOverride = {
					DescriptorSetOverride{.set = 0, .descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()},
					DescriptorSetOverride{.set = 1, .descriptorSet = context->GetSceneDescriptorSet(0)}
				}
			});

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				descriptorSet[f] = Graphics::CreateDescriptorSet(shader, "IrradianceProbeDebug", 2);
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene) return;

			if (!sphereMesh)
			{
				sphereMesh = RenderResourceCache::GetMeshCache(Resources::FindByPath("Skore://Meshes/Sphere.mesh"), false);
			}
			if (!sphereMesh || !sphereMesh->IsLoaded()) return;

			data = context->GetInstanceData<IrradianceVolumeData>("IrradianceVolumeData");
			if (!data || data->volumeBuffer == nullptr || data->volumeCount == 0 || data->irradianceArray == nullptr || data->probeDataArray == nullptr || data->probesPerVolume == 0) return;

			auto showOption = [&](StringView name) -> bool
			{
				PipelineOption opt = context->GetOption(name);
				const bool* value = std::get_if<bool>(&opt);
				return value && *value;
			};

			bool showAll = showOption("Show All Probes");
			bool anyShow = showAll;
			for (u32 i = 0; i < data->volumeCount && !anyShow; ++i)
			{
				if (showOption(CascadeProbeOptionName(i))) anyShow = true;
			}
			if (!anyShow) return;

			u32 frame = context->GetCurrentFrame();
			u64 frameOffset = static_cast<u64>(frame) * MaxIrradianceVolumes * stride;
			u64 range = static_cast<u64>(MaxIrradianceVolumes) * stride;

			GPUDescriptorSet* set = descriptorSet[frame];
			set->UpdateBuffer(0, data->volumeBuffer, frameOffset, range);
			set->UpdateTexture(1, data->irradianceArray);
			set->UpdateSampler(2, Graphics::GetLinearClampToEdgeSampler());
			set->UpdateTexture(3, data->probeDataArray);

			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
			cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
			cmd->BindDescriptorSet(pipeline, 2, set);
			cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

			for (u32 i = 0; i < data->volumeCount; ++i)
			{
				if (!showAll && !showOption(CascadeProbeOptionName(i))) continue;

				IrradianceDebugPushConstants pc{sphereMesh->vertexByteOffset, sphereMesh->vertexLayoutId, data->volumeCount, i};
				cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(pc), &pc);

				for (const MeshPrimitive& prim : sphereMesh->primitives)
				{
					cmd->DrawIndexed(prim.indexCount, data->probesPerVolume, static_cast<u32>(sphereMesh->indexByteOffset / sizeof(u32)) + prim.firstIndex, 0, 0);
				}
			}
		}

		void Destroy() override
		{
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				if (descriptorSet[f]) descriptorSet[f]->Destroy();
			}
			if (pipeline) pipeline->Destroy();
			sphereMesh.reset();
		}
	};

	struct ReflectionPass : RenderPipelinePass
	{
		SK_CLASS(ReflectionPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;
		GPUDescriptorSet* descriptorSet[SK_FRAMES_IN_FLIGHT] = {};
		LightPassInstanceData* lightInstanceData = nullptr;
		BRDFLUTTexture brdfLUT;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::Indirect;

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ReflectionAttachment", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			brdfLUT.Init({512, 512});
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			RID shader = Resources::FindByPath("Skore://Shaders/DefaultIndirectLighting.shader");

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = shader,
				.variant = "Reflection"
			});

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				descriptorSet[f] = Graphics::CreateDescriptorSet(shader, "Reflection", 0);
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (scene->renderObjects.tlas == nullptr) return;

			struct ReflectionPushConstants
			{
				Vec3  cameraPosition;
				float reflectionMultiplier;

				Vec2  outputSize;
				float farClip;
				u32   flags;

				Mat4 proj;
				Mat4 viewProj;
				Mat4 invView;

				int   maxIterations;
				int   maxMipLevel;
				float thickness;
				float rayBias;
				float nearClip;
			};

			ReflectionPushConstants pc;
			pc.cameraPosition = context->camera.cameraPosition;
			pc.reflectionMultiplier = lightInstanceData->reflectionMultiplier;
			pc.outputSize = {static_cast<f32>(context->GetOutputSize().width), static_cast<f32>(context->GetOutputSize().height)};
			pc.farClip = context->camera.farClip;
			pc.nearClip = context->camera.nearClip;
			pc.flags = lightInstanceData->indirectLightFlags;
			pc.proj = context->camera.projection;
			pc.viewProj = context->camera.previousViewProjection;
			pc.invView = context->camera.invView;

			pc.flags |= LightFlags::None;
			pc.maxIterations = 0;
			pc.thickness = 10.0;
			pc.rayBias = 0.01;

			GPUDescriptorSet* set = descriptorSet[context->GetCurrentFrame()];

			set->UpdateTexture(0, context->GetTexture("ReflectionAttachment"));
			set->UpdateTexture(1, context->GetPrevTexture("ColorAttachment"));
			set->UpdateTexture(2, context->GetTexture("GBufferAlbedoMetallic"));
			set->UpdateTexture(3, context->GetTexture("GBufferRoughnessAO"));
			set->UpdateTexture(4, context->GetTexture("GBufferNormals"));
			set->UpdateTexture(5, context->GetTexture(LinearDepthMipChainName));
			set->UpdateTexture(6, lightInstanceData->specularMapTexture);
			set->UpdateTexture(7, brdfLUT.GetTexture());
			set->UpdateSampler(8, brdfLUT.GetSampler());
			set->UpdateSampler(9, Graphics::GetLinearSampler());
			set->UpdateSampler(10, Graphics::GetNearestClampToEdgeSampler());
			set->Update(DescriptorUpdate{.type = DescriptorType::AccelerationStructure, .binding = 11, .topLevelAS = scene->renderObjects.tlas});

			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, set);

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(ReflectionPushConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				descriptorSet[f]->Destroy();
			}
			pipeline->Destroy();
			brdfLUT.Destroy();
		}
	};


	struct IndirectLightingModule : RenderPipelineModule
	{
		SK_CLASS(IndirectLightingModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(IrradianceVolumeUpdatePass));
			setup.passes.EmplaceBack(sktypeid(IrradianceVolumeSamplingPass));
			setup.passes.EmplaceBack(sktypeid(IrradianceVolumeDebugPass));
			setup.passes.EmplaceBack(sktypeid(ReflectionPass));
			setup.options.Insert("Show All Probes", PipelineOption{false});
			for (u32 i = 0; i < MaxIrradianceVolumes; ++i)
			{
				setup.options.Insert(CascadeProbeOptionName(i), PipelineOption{false});
			}
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = "ReflectionAttachment", .type = RenderPipelineResourceType::Attachment, .format = Format::RGBA16_FLOAT, .textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess});
			resources.EmplaceBack(RenderPipelineResource{
				.name = "IrradianceVolumeAttachment",
				.type = RenderPipelineResourceType::Attachment,
				.format = Format::RGBA16_FLOAT,
				.scale = Vec2(1.0, 1.0),
				.textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess
			});
			resources.EmplaceBack(RenderPipelineResource{.name = "IrradianceVolumeData", .type = RenderPipelineResourceType::Instance, .instanceTypeId = sktypeid(IrradianceVolumeData)});
			return resources;
		}
	};

	void RegisterIndirectLightingModule()
	{
		Reflection::Type<IrradianceVolumeData>();
		Reflection::Type<ReflectionPass>();
		Reflection::Type<IndirectLightingModule>();
		Reflection::Type<IrradianceVolumeUpdatePass>();
		Reflection::Type<IrradianceVolumeSamplingPass>();
		Reflection::Type<IrradianceVolumeDebugPass>();
	}
}
