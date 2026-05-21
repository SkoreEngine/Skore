#include "Skore/Scene/Components/RenderComponents.hpp"

#include "Skore/Scene/Components/CharacterController.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/App.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/DebugUtils.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("RenderComponents");

	void RendererComponent::Create()
	{
		ComponentRequireUpdate();
	}

	void RendererComponent::Destroy()
	{
		ClearDrawcalls();
	}

	void RendererComponent::ProcessEvent(const EntityEventDesc& event)
	{
		switch (event.type)
		{
			case EntityEventType::ComponentUpdated:
				Rebuild();
				break;
			case EntityEventType::EntityActivated:
			case EntityEventType::EntityDeactivated:
				ComponentRequireUpdate();
				break;
			case EntityEventType::TransformUpdated:
			{
				const Mat4 worldTransform = entity->GetWorldTransform();
				UpdateAABB();
				for (const DrawcallRef& ref : references)
				{
					scene->renderObjects.UpdateTransform(ref, worldTransform);
				}
				break;
			}
			case EntityEventType::EntityLayerChanged:
			{
				const u64 layerMask = LayerToMask(entity->GetLayer());
				for (const DrawcallRef& ref : references)
				{
					scene->renderObjects.UpdateLayerMask(ref, layerMask);
				}
				break;
			}
			case EntityEventType::CalculateEntityAABB:
			{
				AABB& outAabb = *static_cast<AABB*>(event.eventData);
				outAabb.Expand(aabb);
				break;
			}
			default: break;
		}
	}

	void RendererComponent::SetMesh(RID mesh)
	{
		if (m_mesh == mesh) return;
		m_mesh = mesh;
		ComponentRequireUpdate();
	}

	RID RendererComponent::GetMesh() const
	{
		return m_mesh;
	}

	void RendererComponent::SetCastShadows(bool castShadows)
	{
		if (m_castShadows == castShadows) return;
		m_castShadows = castShadows;
		ComponentRequireUpdate();
	}

	bool RendererComponent::GetCastShadows() const
	{
		return m_castShadows;
	}

	const MaterialArray& RendererComponent::GetMaterials() const
	{
		return m_materials;
	}

	void RendererComponent::SetMaterials(const MaterialArray& materials)
	{
		m_materials = materials;
		ComponentRequireUpdate();
	}

	void RendererComponent::SetMaterial(u32 index, RID material)
	{
		if (m_materials.Size() <= index)
		{
			m_materials.Resize(index + 1);
		}

		m_materials[index] = material;
		ComponentRequireUpdate();
	}

	void RendererComponent::UpdateAABB()
	{
		aabb = AABB();
		if (!m_mesh) return;
		if (MeshResourceCachePtr mc = RenderResourceCache::GetMeshCache(m_mesh, scene->renderObjects.asyncLoad))
		{
			aabb = Math::TransformAABB(mc->aabb, entity->GetWorldTransform());
		}
	}

	void RendererComponent::ClearDrawcalls()
	{
		for (const DrawcallRef& ref : references)
		{
			scene->renderObjects.RemoveDrawcall(ref);
		}
		references.Clear();
	}

	void RendererComponent::Rebuild()
	{
		ClearDrawcalls();

		MeshResourceCachePtr meshCache = m_mesh ? RenderResourceCache::GetMeshCache(m_mesh, scene->renderObjects.asyncLoad) : nullptr;

		if (!entity->IsActive() || !meshCache) return;

		UpdateAABB();

		const Mat4 worldTransform = entity->GetWorldTransform();
		const u64  layerMask      = LayerToMask(entity->GetLayer());
		const u64  userData       = PtrToInt(entity);
		const u8   visibility     = m_castShadows ? DrawcallVisibility::CastShadow : 0;

		references.Resize(meshCache->primitives.Size());

		for (u32 p = 0; p < meshCache->primitives.Size(); ++p)
		{
			MeshPrimitive& primitive = meshCache->primitives[p];

			// Resolve material inline: prefer override, fall back to mesh default.
			RID material;
			if (primitive.materialIndex < m_materials.Size() && m_materials[primitive.materialIndex])
			{
				material = m_materials[primitive.materialIndex];
			}
			if (!material && primitive.materialIndex < meshCache->materials.Size() && meshCache->materials[primitive.materialIndex])
			{
				material = meshCache->materials[primitive.materialIndex]->rid;
			}
			if (!material) continue;

			DrawcallDesc desc{};
			desc.mesh         = meshCache;
			desc.firstIndex   = primitive.firstIndex;
			desc.indexCount   = primitive.indexCount;
			desc.transform    = worldTransform;
			desc.aabb         = primitive.aabb;
			desc.userData     = userData;
			desc.layerMask    = layerMask;
			desc.material     = material;
			desc.bones        = GetBonesDescriptor();
//			desc.blas              = (p < meshCache->blasArray.Size()) ? meshCache->blasArray[p] : nullptr;
			desc.vertexByteOffset  = meshCache->vertexByteOffset;
			desc.indexByteOffset   = meshCache->indexByteOffset;
			desc.vertexLayoutIndex = meshCache->vertexLayoutId;
			desc.visibility        = visibility;

			references[p] = scene->renderObjects.CreateDrawcall(desc, this, p);
		}
	}

	void RendererComponent::RegisterType(NativeReflectType<RendererComponent>& type)
	{
		type.Field<&RendererComponent::m_mesh, &RendererComponent::GetMesh, &RendererComponent::SetMesh>("mesh");
		type.Field<&RendererComponent::m_materials, &RendererComponent::GetMaterials, &RendererComponent::SetMaterials>("materials");
		type.Field<&RendererComponent::m_castShadows, &RendererComponent::GetCastShadows, &RendererComponent::SetCastShadows>("castShadows");

		type.Function<&RendererComponent::SetMaterial>("SetMaterial", "index", "material");
	}

	void StaticMeshRenderer::RegisterType(NativeReflectType<StaticMeshRenderer>& type)
	{
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
	}

	void SkinnedMeshRenderer::SetSkeleton(Entity* skeleton)
	{
		m_skeleton = skeleton;
	}

	Entity* SkinnedMeshRenderer::GetSkeleton() const
	{
		return m_skeleton;
	}

	void SkinnedMeshRenderer::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::SkeletonUpdated)
		{
			SkeletonUpdatedData* data = static_cast<SkeletonUpdatedData*>(event.eventData);
			if (data->entity == m_skeleton)
			{
				UpdateBones(data->skeleton->localToWorldBones);
			}
		}
		else
		{
			RendererComponent::ProcessEvent(event);
		}
	}

	void SkinnedMeshRenderer::Destroy()
	{
		m_skinCache.reset();
		if (m_bonesDescriptor) m_bonesDescriptor->Destroy();
		if (m_bonesBuffer) m_bonesBuffer->Destroy();
		m_bonesDescriptor = nullptr;
		m_bonesBuffer = nullptr;

		RendererComponent::Destroy();
	}

	void SkinnedMeshRenderer::EnsureBonesData()
	{
		if (m_bonesBuffer == nullptr)
		{
			m_bonesBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(Mat4) * MaxBones,
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true
			});

			Mat4* bones = static_cast<Mat4*>(m_bonesBuffer->GetMappedData());
			for (int i = 0; i < MaxBones; ++i)
			{
				new(bones + i) Mat4{Mat4(1.0)};
			}
		}

		if (m_bonesDescriptor == nullptr)
		{
			m_bonesDescriptor = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings{
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::UniformBuffer,
					}
				}
			});
			m_bonesDescriptor->UpdateBuffer(0, m_bonesBuffer, 0, sizeof(Mat4) * MaxBones);
		}
	}

	void SkinnedMeshRenderer::UpdateBones(Span<Mat4> bones)
	{
		if (!m_skinCache || m_skinCache->mesh != m_mesh)
		{
			m_skinCache = RenderResourceCache::GetSkinCache(m_mesh);
			if (m_skinCache) EnsureBonesData();
		}

		if (!m_bonesBuffer || !m_skinCache) return;

		Mat4* data = static_cast<Mat4*>(m_bonesBuffer->GetMappedData());
		for (i32 i = 0; i < bones.Size(); ++i)
		{
			data[i] = bones[i] * m_skinCache->poses[i];
		}
	}

	void SkinnedMeshRenderer::RegisterType(NativeReflectType<SkinnedMeshRenderer>& type)
	{
		type.Field<&SkinnedMeshRenderer::m_skeleton, &SkinnedMeshRenderer::GetSkeleton, &SkinnedMeshRenderer::SetSkeleton>("skeleton");
	}

	void AnimationPlayer::Create()
	{
	}

	void AnimationPlayer::OnStart()
	{
		m_transform = entity->GetComponent<Transform>();
		for (Entity* child : entity->GetChildren())
		{
			if (child->HasFlag(EntityFlags::HasSkeleton))
			{
				m_skeleton = child->GetComponent<Skeleton>();
				if (m_skeleton) break;
			}
		}
		LoadController();
	}

	void AnimationPlayer::SetController(RID controller)
	{
		if (m_controller == controller) return;
		m_controller = controller;
		LoadController();
	}

	RID AnimationPlayer::GetController() const
	{
		return m_controller;
	}

	void AnimationPlayer::LoadController()
	{
		m_layers.Clear();
		m_parameters.Clear();
		m_parameterMap.Clear();

		if (!m_controller || !m_skeleton) return;

		ResourceObject controllerObj = Resources::Read(m_controller);
		if (!controllerObj) return;

		// load parameters
		for (RID paramRID : controllerObj.GetSubObjectList(AnimationControllerResource::Parameters))
		{
			if (ResourceObject paramObj = Resources::Read(paramRID))
			{
				RuntimeAnimParameter param;
				param.type = static_cast<AnimationParameterType>(paramObj.GetUInt(AnimationParameterResource::Type));
				param.floatVal = paramObj.GetFloat(AnimationParameterResource::FloatValue);
				param.intVal = static_cast<i32>(paramObj.GetInt(AnimationParameterResource::IntValue));
				param.boolVal = paramObj.GetBool(AnimationParameterResource::BoolValue);

				String name = paramObj.GetString(AnimationParameterResource::Name);
				m_parameterMap.Insert(name, static_cast<u32>(m_parameters.Size()));
				m_parameters.EmplaceBack(param);
			}
		}

		// build parameter RID â†’ index map for resolving transition conditions
		HashMap<RID, u32> paramRIDToIndex;
		{
			u32 idx = 0;
			for (RID paramRID : controllerObj.GetSubObjectList(AnimationControllerResource::Parameters))
			{
				paramRIDToIndex.Insert(paramRID, idx++);
			}
		}

		// load layers
		for (RID layerRID : controllerObj.GetSubObjectList(AnimationControllerResource::Layers))
		{
			if (ResourceObject layerObj = Resources::Read(layerRID))
			{
				RuntimeAnimLayer layer;
				layer.name = layerObj.GetString(AnimationLayerResource::Name);
				layer.weight = layerObj.GetFloat(AnimationLayerResource::Weight);
				if (layer.weight <= 0.0f) layer.weight = 1.0f;
				layer.blendMode = static_cast<AnimationLayerBlendMode>(layerObj.GetUInt(AnimationLayerResource::BlendMode));
				layer.rootMotionMode = static_cast<RootMotionMode>(layerObj.GetUInt(AnimationLayerResource::RootMotion));
				layer.rootMotionAxes = static_cast<RootMotionAxes>(layerObj.GetUInt(AnimationLayerResource::RootMotionAxis));
				layer.applyRotation = layerObj.GetBool(AnimationLayerResource::ApplyRotation);

				// build state RID â†’ index map
				HashMap<RID, u32> stateRIDToIndex;
				Span<RID> stateRIDs = layerObj.GetSubObjectList(AnimationLayerResource::States);

				for (u32 si = 0; si < stateRIDs.Size(); ++si)
				{
					stateRIDToIndex.Insert(stateRIDs[si], si);
				}

				// load states
				for (RID stateRID : stateRIDs)
				{
					if (ResourceObject stateObj = Resources::Read(stateRID))
					{
						RuntimeAnimState state;
						state.name = stateObj.GetString(AnimationStateResource::Name);
						state.speed = stateObj.GetFloat(AnimationStateResource::Speed);
						if (state.speed == 0.0f) state.speed = 1.0f;

						RID animRID = stateObj.GetReference(AnimationStateResource::Animation);
						if (animRID)
						{
							LoadClipData(state.clip, animRID);
						}

						layer.states.EmplaceBack(Traits::Move(state));
					}
				}

				// resolve default state
				RID defaultStateRID = layerObj.GetReference(AnimationLayerResource::DefaultState);
				if (defaultStateRID)
				{
					if (auto it = stateRIDToIndex.Find(defaultStateRID))
					{
						layer.defaultState = it->second;
					}
				}
				layer.currentState = layer.defaultState;

				// load transitions
				for (RID transRID : layerObj.GetSubObjectList(AnimationLayerResource::Transitions))
				{
					if (ResourceObject transObj = Resources::Read(transRID))
					{
						RuntimeAnimTransition transition;

						RID fromRID = transObj.GetReference(AnimationTransitionResource::From);
						RID toRID = transObj.GetReference(AnimationTransitionResource::To);

						if (auto it = stateRIDToIndex.Find(fromRID)) transition.fromState = it->second;
						if (auto it = stateRIDToIndex.Find(toRID)) transition.toState = it->second;

						transition.crossFadeTime = transObj.GetFloat(AnimationTransitionResource::CrossFadeTime);
						if (transition.crossFadeTime <= 0.0f) transition.crossFadeTime = 0.001f;
						transition.hasExitTime = transObj.GetBool(AnimationTransitionResource::HasExitTime);
						transition.exitTime = transObj.GetFloat(AnimationTransitionResource::ExitTime);
						transition.fixedDuration = transObj.GetBool(AnimationTransitionResource::FixedDuration);
						transition.transitionOffset = transObj.GetFloat(AnimationTransitionResource::TransitionOffset);

						for (RID condRID : transObj.GetSubObjectList(AnimationTransitionResource::Conditions))
						{
							if (ResourceObject condObj = Resources::Read(condRID))
							{
								RuntimeAnimCondition cond;
								RID paramRef = condObj.GetReference(AnimationTransitionConditionResource::Parameter);
								if (auto it = paramRIDToIndex.Find(paramRef))
								{
									cond.paramIndex = it->second;
								}
								cond.condition = static_cast<AnimationTransitionCondition>(condObj.GetUInt(AnimationTransitionConditionResource::Condition));
								cond.value = condObj.GetFloat(AnimationTransitionConditionResource::Value);
								transition.conditions.EmplaceBack(cond);
							}
						}

						layer.transitions.EmplaceBack(Traits::Move(transition));
					}
				}

				// build bone mask from avatar
				RID avatarRID = layerObj.GetReference(AnimationLayerResource::Avatar);
				BuildBoneMask(layer, avatarRID);

				if (layer.rootMotionMode != RootMotionMode::None)
				{
					layer.rootBoneIndex = FindRootBoneIndex();
				}

				m_layers.EmplaceBack(Traits::Move(layer));
			}
		}
	}

	void AnimationPlayer::LoadClipData(AnimClipData& clip, RID animationRID)
	{
		clip.channels.Clear();

		ResourceObject animObj = Resources::Read(animationRID);
		if (!animObj) return;

		ResourceBuffer buffer = animObj.GetBuffer(AnimationClipResource::KeyFramesBuffer);

		ByteBuffer dataBuffer;
		dataBuffer.Resize(buffer.GetSize());
		buffer.CopyData(dataBuffer.begin(), dataBuffer.Size(), 0);

		clip.frames = animObj.GetUInt(AnimationClipResource::NumFrames);
		clip.duration = animObj.GetFloat(AnimationClipResource::Duration);
		clip.frameRate = animObj.GetFloat(AnimationClipResource::FrameRate);
		clip.timeBegin = animObj.GetFloat(AnimationClipResource::TimeBegin);
		clip.timeEnd = animObj.GetFloat(AnimationClipResource::TimeEnd);

		// build bone name â†’ skeleton index map
		HashMap<String, u32> boneNameToIndex;
		if (m_skeleton)
		{
			for (u32 i = 0; i < m_skeleton->bones.Size(); ++i)
			{
				boneNameToIndex.Insert(m_skeleton->bones[i].name, i);
			}
		}

		for (RID channelRID : animObj.GetSubObjectList(AnimationClipResource::Channels))
		{
			if (ResourceObject channelObj = Resources::Read(channelRID))
			{
				String boneName = channelObj.GetString(AnimationChannelResource::Name);

				auto it = boneNameToIndex.Find(boneName);
				if (!it) continue; // skip channels for bones not in skeleton

				AnimChannel channel;
				channel.boneIndex = it->second;
				channel.positions.Resize(clip.frames);
				channel.rotations.Resize(clip.frames);
				channel.scales.Resize(clip.frames);

				u64 offset = channelObj.GetUInt(AnimationChannelResource::BufferOffset);

				for (u32 i = 0; i < clip.frames; ++i)
				{
					AnimationKeyFrame* keyframe = reinterpret_cast<AnimationKeyFrame*>(dataBuffer.begin() + offset + i * sizeof(AnimationKeyFrame));
					channel.positions[i] = keyframe->position;
					channel.rotations[i] = keyframe->rotation;
					channel.scales[i] = keyframe->scale;
				}

				clip.channels.EmplaceBack(Traits::Move(channel));
			}
		}
	}

	void AnimationPlayer::BuildBoneMask(RuntimeAnimLayer& layer, RID avatarRID)
	{
		if (!m_skeleton) return;

		u32 boneCount = static_cast<u32>(m_skeleton->bones.Size());
		layer.boneMask.Resize(boneCount);

		if (!avatarRID)
		{
			// no avatar = all bones enabled
			for (u32 i = 0; i < boneCount; ++i)
			{
				layer.boneMask[i] = true;
			}
			return;
		}

		// start with all disabled
		for (u32 i = 0; i < boneCount; ++i)
		{
			layer.boneMask[i] = false;
		}

		ResourceObject avatarObj = Resources::Read(avatarRID);
		if (!avatarObj) return;

		// build nameâ†’index for skeleton
		HashMap<String, u32> boneNameToIndex;
		for (u32 i = 0; i < m_skeleton->bones.Size(); ++i)
		{
			boneNameToIndex.Insert(m_skeleton->bones[i].name, i);
		}

		// recursive walk of avatar bone tree
		struct WalkAvatar
		{
			static void Walk(RID boneRID, Array<bool>& mask, const HashMap<String, u32>& nameMap)
			{
				ResourceObject boneObj = Resources::Read(boneRID);
				if (!boneObj) return;

				String name = boneObj.GetString(AnimationAvatarBoneResource::BoneName);
				bool enabled = boneObj.GetBool(AnimationAvatarBoneResource::Enabled);

				if (enabled)
				{
					if (auto it = nameMap.Find(name))
					{
						mask[it->second] = true;
					}
				}

				for (RID childRID : boneObj.GetSubObjectList(AnimationAvatarBoneResource::Children))
				{
					Walk(childRID, mask, nameMap);
				}
			}
		};

		RID rootBoneRID = avatarObj.GetSubObject(AnimationAvatarResource::RootBone);
		if (rootBoneRID)
		{
			WalkAvatar::Walk(rootBoneRID, layer.boneMask, boneNameToIndex);
		}
	}

	void AnimationPlayer::EvaluateTransitions(RuntimeAnimLayer& layer, f32 dt)
	{
		if (layer.transitioning)
		{
			layer.fadeElapsed += dt;
			if (layer.fadeElapsed >= layer.fadeDuration)
			{
				// transition complete
				layer.transitioning = false;
				layer.prevState = U32_MAX;
				layer.fadeElapsed = 0.0f;
			}
			return;
		}

		// compute normalized time for exit time checks
		f32 normalizedTime = 0.0f;
		if (layer.currentState < layer.states.Size())
		{
			const AnimClipData& clip = layer.states[layer.currentState].clip;
			f32 animDuration = clip.timeEnd - clip.timeBegin;
			if (animDuration > 0.0f)
			{
				normalizedTime = layer.currentTime / animDuration;
			}
		}

		// check outgoing transitions from current state
		for (const auto& transition : layer.transitions)
		{
			if (transition.fromState != layer.currentState) continue;

			// exit time gate: only allow transition after source clip reaches exitTime
			if (transition.hasExitTime)
			{
				if (normalizedTime < transition.exitTime) continue;
			}

			if (CheckConditions(transition.conditions))
			{
				// compute fade duration
				f32 fadeDuration = transition.crossFadeTime;
				if (!transition.fixedDuration && layer.currentState < layer.states.Size())
				{
					const AnimClipData& clip = layer.states[layer.currentState].clip;
					f32 animDuration = clip.timeEnd - clip.timeBegin;
					fadeDuration = transition.crossFadeTime * animDuration;
				}
				if (fadeDuration <= 0.0f) fadeDuration = 0.001f;

				// start transition
				layer.prevState = layer.currentState;
				layer.prevTime = layer.currentTime;
				layer.currentState = transition.toState;
				layer.fadeDuration = fadeDuration;
				layer.fadeElapsed = 0.0f;
				layer.transitioning = true;

				// transition offset: start destination clip at a specific point
				if (transition.transitionOffset > 0.0f && layer.currentState < layer.states.Size())
				{
					const AnimClipData& destClip = layer.states[layer.currentState].clip;
					f32 destDuration = destClip.timeEnd - destClip.timeBegin;
					layer.currentTime = transition.transitionOffset * destDuration;
				}
				else
				{
					layer.currentTime = 0.0f;
				}

				break;
			}
		}
	}

	bool AnimationPlayer::CheckConditions(const Array<RuntimeAnimCondition>& conditions) const
	{
		for (const auto& cond : conditions)
		{
			if (cond.paramIndex >= m_parameters.Size()) return false;

			const RuntimeAnimParameter& param = m_parameters[cond.paramIndex];
			bool pass = false;

			switch (cond.condition)
			{
			case AnimationTransitionCondition::Greater:
				pass = (param.type == AnimationParameterType::Float) ? param.floatVal > cond.value : param.intVal > static_cast<i32>(cond.value);
				break;
			case AnimationTransitionCondition::Less:
				pass = (param.type == AnimationParameterType::Float) ? param.floatVal < cond.value : param.intVal < static_cast<i32>(cond.value);
				break;
			case AnimationTransitionCondition::Equal:
				pass = (param.type == AnimationParameterType::Float) ? param.floatVal == cond.value : param.intVal == static_cast<i32>(cond.value);
				break;
			case AnimationTransitionCondition::NotEqual:
				pass = (param.type == AnimationParameterType::Float) ? param.floatVal != cond.value : param.intVal != static_cast<i32>(cond.value);
				break;
			case AnimationTransitionCondition::True:
				pass = (param.type == AnimationParameterType::Trigger) ? param.boolVal : param.boolVal;
				break;
			case AnimationTransitionCondition::False:
				pass = !param.boolVal;
				break;
			}

			if (!pass) return false;
		}

		return true;
	}

	void AnimationPlayer::SampleClip(const AnimClipData& clip, f32 time, Array<BoneNode>& bones, const Array<bool>& boneMask, bool first, f32 weight)
	{
		if (clip.frames == 0) return;

		f32 animDuration = clip.timeEnd - clip.timeBegin;

		if (animDuration > 0.0f)
		{
			while (time >= animDuration) time -= animDuration;
			while (time < 0.0f) time += animDuration;
		}

		f32 frameTime = time * clip.frameRate;
		u32 frameIndex = static_cast<u32>(frameTime);
		u32 f0 = Math::Min(frameIndex, clip.frames - 1);
		u32 f1 = Math::Min(frameIndex + 1, clip.frames - 1);
		f32 t = frameTime - static_cast<f32>(frameIndex);

		for (const auto& channel : clip.channels)
		{
			if (channel.boneIndex >= bones.Size()) continue;
			if (channel.boneIndex < boneMask.Size() && !boneMask[channel.boneIndex]) continue;

			Vec3 pos = Vec3::Mix(channel.positions[f0], channel.positions[f1], t);
			Quat rot = Quat::Slerp(channel.rotations[f0], channel.rotations[f1], t);
			Vec3 scl = Vec3::Mix(channel.scales[f0], channel.scales[f1], t);

			BoneNode& bone = bones[channel.boneIndex];

			if (first)
			{
				bone.position = pos * weight;
				bone.rotation = rot;
				bone.scale = scl * weight;
			}
			else
			{
				bone.position = bone.position + pos * weight;
				bone.rotation = Quat::Slerp(bone.rotation, rot, weight);
				bone.scale = bone.scale + scl * weight;
			}
		}
	}

	void AnimationPlayer::ConsumeTriggers()
	{
		for (auto& param : m_parameters)
		{
			if (param.type == AnimationParameterType::Trigger && param.boolVal)
			{
				param.boolVal = false;
			}
		}
	}

	// Parameter API
	void AnimationPlayer::SetFloat(const String& name, f32 value)
	{
		if (auto it = m_parameterMap.Find(name))
		{
			m_parameters[it->second].floatVal = value;
		}
	}

	void AnimationPlayer::SetInt(const String& name, i32 value)
	{
		if (auto it = m_parameterMap.Find(name))
		{
			m_parameters[it->second].intVal = value;
		}
	}

	void AnimationPlayer::SetBool(const String& name, bool value)
	{
		if (auto it = m_parameterMap.Find(name))
		{
			m_parameters[it->second].boolVal = value;
		}
	}

	void AnimationPlayer::SetTrigger(const String& name)
	{
		if (auto it = m_parameterMap.Find(name))
		{
			m_parameters[it->second].boolVal = true;
		}
	}

	f32 AnimationPlayer::GetFloat(const String& name) const
	{
		if (auto it = m_parameterMap.Find(name))
		{
			return m_parameters[it->second].floatVal;
		}
		return 0.0f;
	}

	i32 AnimationPlayer::GetInt(const String& name) const
	{
		if (auto it = m_parameterMap.Find(name))
		{
			return m_parameters[it->second].intVal;
		}
		return 0;
	}

	bool AnimationPlayer::GetBool(const String& name) const
	{
		if (auto it = m_parameterMap.Find(name))
		{
			return m_parameters[it->second].boolVal;
		}
		return false;
	}

	u32 AnimationPlayer::FindRootBoneIndex() const
	{
		if (!m_skeleton) return U32_MAX;
		for (u32 i = 0; i < m_skeleton->bones.Size(); ++i)
		{
			if (m_skeleton->bones[i].parentIndex >= U32_MAX)
			{
				return i;
			}
		}
		return U32_MAX;
	}

	void AnimationPlayer::ExtractRootMotionDelta(const AnimClipData& clip, f32 currentTime, f32 prevTime, f32 speed, Vec3& outPos, Quat& outRot)
	{
		outPos = {};
		outRot = {0, 0, 0, 1};

		if (clip.frames == 0 || !m_skeleton) return;

		// find root bone channel
		u32 rootBoneIdx = U32_MAX;
		for (u32 i = 0; i < m_layers.Size(); ++i)
		{
			if (m_layers[i].rootBoneIndex != U32_MAX)
			{
				rootBoneIdx = m_layers[i].rootBoneIndex;
				break;
			}
		}
		if (rootBoneIdx == U32_MAX) return;

		const AnimChannel* rootChannel = nullptr;
		for (const auto& channel : clip.channels)
		{
			if (channel.boneIndex == rootBoneIdx)
			{
				rootChannel = &channel;
				break;
			}
		}
		if (!rootChannel) return;

		f32 animDuration = clip.timeEnd - clip.timeBegin;
		if (animDuration <= 0.0f) return;

		// helper to sample position/rotation at a given time
		auto sampleAt = [&](f32 t, Vec3& pos, Quat& rot)
		{
			while (t >= animDuration) t -= animDuration;
			while (t < 0.0f) t += animDuration;

			f32 frameTime = t * clip.frameRate;
			u32 frameIndex = static_cast<u32>(frameTime);
			u32 f0 = Math::Min(frameIndex, clip.frames - 1);
			u32 f1 = Math::Min(frameIndex + 1, clip.frames - 1);
			f32 frac = frameTime - static_cast<f32>(frameIndex);

			pos = Vec3::Mix(rootChannel->positions[f0], rootChannel->positions[f1], frac);
			rot = Quat::Slerp(rootChannel->rotations[f0], rootChannel->rotations[f1], frac);
		};

		// normalize times to detect loop wrap-around (raw times keep incrementing past duration)
		auto wrapTime = [&](f32 t) -> f32
		{
			while (t >= animDuration) t -= animDuration;
			while (t < 0.0f) t += animDuration;
			return t;
		};

		f32 normPrev = wrapTime(prevTime);
		f32 normCurr = wrapTime(currentTime);
		bool looped = (speed >= 0.0f) ? (normCurr < normPrev) : (normCurr > normPrev);

		if (!looped)
		{
			Vec3 posPrev, posCurr;
			Quat rotPrev, rotCurr;
			sampleAt(normPrev, posPrev, rotPrev);
			sampleAt(normCurr, posCurr, rotCurr);

			outPos = posCurr - posPrev;
			Quat conjugate = {-rotPrev.x, -rotPrev.y, -rotPrev.z, rotPrev.w};
			outRot = conjugate * rotCurr;
		}
		else
		{
			// segment A: prevTime â†’ end
			Vec3 posA0, posA1;
			Quat rotA0, rotA1;
			sampleAt(normPrev, posA0, rotA0);
			sampleAt(animDuration - 0.0001f, posA1, rotA1);

			Vec3 deltaA = posA1 - posA0;
			Quat conjA = {-rotA0.x, -rotA0.y, -rotA0.z, rotA0.w};
			Quat deltaRotA = conjA * rotA1;

			// segment B: begin â†’ currentTime
			Vec3 posB0, posB1;
			Quat rotB0, rotB1;
			sampleAt(0.0f, posB0, rotB0);
			sampleAt(normCurr, posB1, rotB1);

			Vec3 deltaB = posB1 - posB0;
			Quat conjB = {-rotB0.x, -rotB0.y, -rotB0.z, rotB0.w};
			Quat deltaRotB = conjB * rotB1;

			outPos = deltaA + deltaB;
			outRot = deltaRotA * deltaRotB;
		}
	}

	void AnimationPlayer::ApplyRootMotion(RuntimeAnimLayer& layer, f32 dt)
	{
		if (!m_transform || layer.rootBoneIndex == U32_MAX) return;

		Vec3 worldDelta = m_transform->GetRotation() * layer.rootDeltaPosition;

		if (layer.rootMotionAxes == RootMotionAxes::XZ)
		{
			worldDelta.y = 0.0f;
		}

		if (layer.rootMotionMode == RootMotionMode::Transform)
		{
			m_transform->SetPosition(m_transform->GetPosition() + worldDelta);

			if (layer.applyRotation)
			{
				f32 yaw = Quat::Yaw(layer.rootDeltaRotation);
				Quat yawRot = Quat::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
				m_transform->SetRotation(m_transform->GetRotation() * yawRot);
			}
		}
		else if (layer.rootMotionMode == RootMotionMode::Velocity)
		{
			if (dt > 0.0f)
			{
				Vec3 velocity = worldDelta / dt;

				CharacterController* cc = entity->GetComponent<CharacterController>();
				if (cc)
				{
					Vec3 currentVel = cc->GetLinearVelocity();
					velocity.y = currentVel.y; // preserve gravity
					cc->SetLinearVelocity(velocity);
				}
			}
		}
	}

	void AnimationPlayer::SetApplyRootMotion(bool value)
	{
		m_applyRootMotion = value;
	}

	bool AnimationPlayer::GetApplyRootMotion() const
	{
		return m_applyRootMotion;
	}

	Vec3 AnimationPlayer::GetRootMotionDelta() const
	{
		Vec3 total = {};
		for (const auto& layer : m_layers)
		{
			if (layer.rootMotionMode != RootMotionMode::None)
			{
				total = total + layer.rootDeltaPosition;
			}
		}
		return total;
	}

	Quat AnimationPlayer::GetRootMotionDeltaRotation() const
	{
		Quat total = {0, 0, 0, 1};
		for (const auto& layer : m_layers)
		{
			if (layer.rootMotionMode != RootMotionMode::None)
			{
				total = total * layer.rootDeltaRotation;
			}
		}
		return total;
	}

	void AnimationPlayer::OnUpdate(f64 deltaTime)
	{
		if (m_layers.Empty() || !m_skeleton) return;

		f32 dt = static_cast<f32>(deltaTime);

		for (u32 layerIdx = 0; layerIdx < m_layers.Size(); ++layerIdx)
		{
			RuntimeAnimLayer& layer = m_layers[layerIdx];
			if (layer.states.Empty()) continue;

			// evaluate transitions
			EvaluateTransitions(layer, dt);

			// save previous times before advancing
			layer.prevCurrentTime = layer.currentTime;
			layer.prevPrevTime = layer.prevTime;

			// advance time
			if (layer.currentState < layer.states.Size())
			{
				layer.currentTime += dt * layer.states[layer.currentState].speed;
			}

			if (layer.transitioning && layer.prevState < layer.states.Size())
			{
				layer.prevTime += dt * layer.states[layer.prevState].speed;
			}

			// override: replaces masked bones; additive: adds on top of existing pose
			bool isOverride = (layer.blendMode == AnimationLayerBlendMode::Override);

			if (layer.transitioning && layer.prevState < layer.states.Size())
			{
				f32 fadeWeight = layer.fadeElapsed / layer.fadeDuration;
				fadeWeight = Math::Clamp(fadeWeight, 0.0f, 1.0f);

				// sample each clip into a full independent pose, then blend
				// this avoids artifacts from bones missing in one clip
				Array<BoneNode> poseA = m_skeleton->bones;
				Array<BoneNode> poseB = m_skeleton->bones;

				SampleClip(layer.states[layer.prevState].clip, layer.prevTime, poseA, layer.boneMask, true, 1.0f);
				SampleClip(layer.states[layer.currentState].clip, layer.currentTime, poseB, layer.boneMask, true, 1.0f);

				for (u32 bi = 0; bi < m_skeleton->bones.Size(); ++bi)
				{
					if (bi < layer.boneMask.Size() && !layer.boneMask[bi]) continue;

					BoneNode& bone = m_skeleton->bones[bi];
					const BoneNode& bA = poseA[bi];
					const BoneNode& bB = poseB[bi];

					Vec3 blendedPos = Vec3::Mix(bA.position, bB.position, fadeWeight);
					Quat blendedRot = Quat::Slerp(bA.rotation, bB.rotation, fadeWeight);
					Vec3 blendedScl = Vec3::Mix(bA.scale, bB.scale, fadeWeight);

					if (isOverride)
					{
						bone.position = blendedPos;
						bone.rotation = blendedRot;
						bone.scale = blendedScl;
					}
					else
					{
						bone.position = bone.position + (blendedPos - bone.position) * layer.weight;
						bone.rotation = Quat::Slerp(bone.rotation, blendedRot, layer.weight);
						bone.scale = bone.scale + (blendedScl - bone.scale) * layer.weight;
					}
				}
			}
			else if (layer.currentState < layer.states.Size())
			{
				SampleClip(layer.states[layer.currentState].clip, layer.currentTime, m_skeleton->bones, layer.boneMask, isOverride, layer.weight);
			}

			// extract root motion delta and zero root bone movement
			if (m_applyRootMotion && layer.rootMotionMode != RootMotionMode::None && layer.rootBoneIndex != U32_MAX)
			{
				layer.rootDeltaPosition = {};
				layer.rootDeltaRotation = {0, 0, 0, 1};

				if (layer.transitioning && layer.prevState < layer.states.Size())
				{
					f32 fadeWeight = layer.fadeElapsed / layer.fadeDuration;
					fadeWeight = Math::Clamp(fadeWeight, 0.0f, 1.0f);

					Vec3 deltaPosA, deltaPosB;
					Quat deltaRotA, deltaRotB;

					f32 prevSpeed = layer.states[layer.prevState].speed;
					f32 currSpeed = layer.states[layer.currentState].speed;

					ExtractRootMotionDelta(layer.states[layer.prevState].clip, layer.prevTime, layer.prevPrevTime, prevSpeed, deltaPosA, deltaRotA);
					ExtractRootMotionDelta(layer.states[layer.currentState].clip, layer.currentTime, layer.prevCurrentTime, currSpeed, deltaPosB, deltaRotB);

					layer.rootDeltaPosition = deltaPosA * (1.0f - fadeWeight) + deltaPosB * fadeWeight;
					layer.rootDeltaRotation = Quat::Slerp(deltaRotA, deltaRotB, fadeWeight);
				}
				else if (layer.currentState < layer.states.Size())
				{
					f32 currSpeed = layer.states[layer.currentState].speed;
					ExtractRootMotionDelta(layer.states[layer.currentState].clip, layer.currentTime, layer.prevCurrentTime, currSpeed, layer.rootDeltaPosition, layer.rootDeltaRotation);
				}

				// zero root bone's extracted components
				BoneNode& rootBone = m_skeleton->bones[layer.rootBoneIndex];
				if (layer.rootMotionAxes == RootMotionAxes::XYZ)
				{
					rootBone.position = {};
				}
				else
				{
					rootBone.position.x = 0.0f;
					rootBone.position.z = 0.0f;
				}

				if (layer.applyRotation)
				{
					f32 yaw = Quat::Yaw(rootBone.rotation);
					Quat inverseYaw = Quat::AngleAxis(-yaw, Vec3(0.0f, 1.0f, 0.0f));
					rootBone.rotation = inverseYaw * rootBone.rotation;
				}
			}
		}

		m_skeleton->UpdateWorldBones();

		// apply root motion after bone update
		if (m_applyRootMotion)
		{
			for (u32 layerIdx = 0; layerIdx < m_layers.Size(); ++layerIdx)
			{
				RuntimeAnimLayer& layer = m_layers[layerIdx];
				if (layer.rootMotionMode != RootMotionMode::None)
				{
					ApplyRootMotion(layer, dt);
				}
			}
		}

		ConsumeTriggers();
	}

	void AnimationPlayer::RegisterType(NativeReflectType<AnimationPlayer>& type)
	{
		type.Field<&AnimationPlayer::m_controller, &AnimationPlayer::GetController, &AnimationPlayer::SetController>("controller");
		type.Field<&AnimationPlayer::m_applyRootMotion, &AnimationPlayer::GetApplyRootMotion, &AnimationPlayer::SetApplyRootMotion>("applyRootMotion");

		type.Function<&AnimationPlayer::SetFloat>("SetFloat", "name", "value");
		type.Function<&AnimationPlayer::SetInt>("SetInt", "name", "value");
		type.Function<&AnimationPlayer::SetBool>("SetBool", "name", "value");
		type.Function<&AnimationPlayer::SetTrigger>("SetTrigger", "name");
		type.Function<&AnimationPlayer::GetFloat>("GetFloat", "name");
		type.Function<&AnimationPlayer::GetInt>("GetInt", "name");
		type.Function<&AnimationPlayer::GetBool>("GetBool", "name");
		type.Function<&AnimationPlayer::GetRootMotionDelta>("GetRootMotionDelta");
		type.Function<&AnimationPlayer::GetRootMotionDeltaRotation>("GetRootMotionDeltaRotation");
	}

	void BoneNode::RegisterType(NativeReflectType<BoneNode>& type)
	{
		type.Field<&BoneNode::name>("name");
		type.Field<&BoneNode::parentIndex>("parentIndex");
		type.Field<&BoneNode::position>("position");
		type.Field<&BoneNode::rotation>("rotation");
		type.Field<&BoneNode::scale>("scale");
	}

	void Skeleton::Create()
	{
		entity->AddFlag(EntityFlags::HasSkeleton);
	}

	void Skeleton::SetBones(const Array<BoneNode>& pBones)
	{
		bones = pBones;
		localToWorldBones.Resize(bones.Size());
		UpdateWorldBones();
	}

	const Array<BoneNode>& Skeleton::GetBones() const
	{
		return bones;
	}

	void Skeleton::RegisterType(NativeReflectType<Skeleton>& type)
	{
		type.Field<&Skeleton::bones, &Skeleton::GetBones, &Skeleton::SetBones>("bones");
	}

	void Skeleton::UpdateWorldBones()
	{
		for (u32 i = 0; i < bones.Size(); i++)
		{
			Mat4 localTransform = Mat4::Translate(Mat4{1.0}, bones[i].position) * Quat::ToMatrix4(bones[i].rotation) * Mat4::Scale(Mat4{1.0}, bones[i].scale);
			if (bones[i].parentIndex < U32_MAX)
			{
				localToWorldBones[i] = localToWorldBones[bones[i].parentIndex] * localTransform;
			}
			else
			{
				localToWorldBones[i] = localTransform;
			}
		}

		if (entity->GetParent() != nullptr)
		{
			SkeletonUpdatedData data = {};
			data.entity = entity;
			data.skeleton = this;

			EntityEventDesc event;
			event.type = EntityEventType::SkeletonUpdated;
			event.eventData = &data;

			entity->GetParent()->NotifyEvent(event, true);
		}
	}
}
