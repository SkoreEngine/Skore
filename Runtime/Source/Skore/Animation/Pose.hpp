#pragma once

#include "AnimXForm.hpp"
#include "AnimationSkeleton.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	// A pose is one parent-space (local) XForm per bone, bone-index aligned 1:1 with its
	// Anim::Skeleton (and therefore with the scene Skeleton::bones). A lazily-computed
	// model-space cache and a State enum (for blender fast-paths / additive detection)
	// ride alongside. Faithful port of Esoterica's Pose. Spec §3.2.
	class SK_API Pose
	{
	public:
		enum class Type : u8
		{
			None = 0,
			ReferencePose,
			ZeroPose
		};

		enum class State : u8
		{
			Unset = 0,
			Pose,
			ReferencePose,
			ZeroPose,
			AdditivePose
		};

	public:
		Pose() = default; // empty; assign a skeleton-bound pose before use
		explicit Pose(const Skeleton* skeleton, Type initialType = Type::ReferencePose);

		void CopyFrom(const Pose& rhs);
		void CopyFrom(const Pose* rhs) { CopyFrom(*rhs); }

		const Skeleton* GetSkeleton() const { return m_skeleton; }
		i32             GetNumBones() const { return static_cast<i32>(m_parentSpaceTransforms.Size()); }
		i32             GetNumBones(Skeleton::LOD lod) const { return m_skeleton->GetNumBones(lod); }

		// Pose state
		//-------------------------------------------------------------------------
		void Reset(Type initState = Type::None, bool calculateModelSpacePose = false);

		bool IsPoseSet() const { return m_state != State::Unset; }
		bool IsReferencePose() const { return m_state == State::ReferencePose; }
		bool IsZeroPose() const { return m_state == State::ZeroPose; }
		bool IsAdditivePose() const { return m_state == State::AdditivePose; }
		void SetState(State state) { m_state = state; }

		// Parent-space (local) transforms — always valid
		//-------------------------------------------------------------------------
		const Array<XForm>& GetTransforms() const { return m_parentSpaceTransforms; }
		Array<XForm>&       GetWritableTransforms() { return m_parentSpaceTransforms; }

		const XForm& GetTransform(i32 boneIdx) const { return m_parentSpaceTransforms[boneIdx]; }

		void SetTransform(i32 boneIdx, const XForm& transform)
		{
			m_parentSpaceTransforms[boneIdx] = transform;
			MarkAsValidPose();
		}

		void SetRotation(i32 boneIdx, const Quat& rotation)
		{
			m_parentSpaceTransforms[boneIdx].m_rotation = rotation;
			MarkAsValidPose();
		}

		void SetTranslation(i32 boneIdx, const Vec3& translation)
		{
			m_parentSpaceTransforms[boneIdx].m_translation = translation;
			MarkAsValidPose();
		}

		void SetScale(i32 boneIdx, f32 uniformScale)
		{
			m_parentSpaceTransforms[boneIdx].m_scale = uniformScale;
			MarkAsValidPose();
		}

		// Model-space (global) transform cache — may be empty (stale)
		//-------------------------------------------------------------------------
		bool                HasModelSpaceTransforms() const { return !m_modelSpaceTransforms.Empty(); }
		void                ClearModelSpaceTransforms() { m_modelSpaceTransforms.Clear(); }
		const Array<XForm>& GetModelSpaceTransforms() const { return m_modelSpaceTransforms; }
		void                CalculateModelSpaceTransforms(Skeleton::LOD lod = Skeleton::LOD::High);
		XForm               GetModelSpaceTransform(i32 boneIdx) const;

	private:
		void SetToReferencePose(bool setModelSpacePose);
		void SetToZeroPose(bool setModelSpacePose);

		// Any direct local edit promotes Unset/Reference/Zero -> Pose (but keeps Additive).
		void MarkAsValidPose()
		{
			if (m_state != State::Pose && m_state != State::AdditivePose)
			{
				m_state = State::Pose;
			}
		}

	private:
		const Skeleton* m_skeleton = nullptr;       // the skeleton for this pose
		Array<XForm>    m_parentSpaceTransforms;     // parent-space (local), ALWAYS valid
		Array<XForm>    m_modelSpaceTransforms;      // model-space (global) cache, may be empty
		State           m_state = State::Unset;
	};
}
