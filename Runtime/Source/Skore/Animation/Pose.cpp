#include "Pose.hpp"

namespace Skore::Anim
{
	Pose::Pose(const Skeleton* skeleton, Type initialType)
		: m_skeleton(skeleton)
	{
		m_parentSpaceTransforms.Resize(skeleton->GetNumBones());
		Reset(initialType);
	}

	void Pose::CopyFrom(const Pose& rhs)
	{
		m_skeleton = rhs.m_skeleton;
		m_parentSpaceTransforms = rhs.m_parentSpaceTransforms;
		m_modelSpaceTransforms = rhs.m_modelSpaceTransforms;
		m_state = rhs.m_state;
	}

	void Pose::Reset(Type initialState, bool calculateModelSpacePose)
	{
		switch (initialState)
		{
			case Type::ReferencePose:
				SetToReferencePose(calculateModelSpacePose);
				break;

			case Type::ZeroPose:
				SetToZeroPose(calculateModelSpacePose);
				break;

			default:
				// Leave the transform memory intact, just mark the pose unset.
				m_state = State::Unset;
				break;
		}
	}

	void Pose::SetToReferencePose(bool setModelSpacePose)
	{
		m_parentSpaceTransforms = m_skeleton->GetParentSpaceReferencePose();

		if (setModelSpacePose)
		{
			m_modelSpaceTransforms = m_skeleton->GetModelSpaceReferencePose();
		}
		else
		{
			m_modelSpaceTransforms.Clear();
		}

		m_state = State::ReferencePose;
	}

	void Pose::SetToZeroPose(bool setModelSpacePose)
	{
		// The zero pose is the additive identity: identity rotation, zero translation.
		// NOTE: scale is left at XForm's identity (1.0). Additive-scale semantics are
		// finalized when the Blender's additive path lands (M1/Blender turn).
		m_parentSpaceTransforms.Clear();
		m_parentSpaceTransforms.Resize(m_skeleton->GetNumBones(), XForm::Identity());

		if (setModelSpacePose)
		{
			m_modelSpaceTransforms = m_parentSpaceTransforms;
		}
		else
		{
			m_modelSpaceTransforms.Clear();
		}

		m_state = State::ZeroPose;
	}

	void Pose::CalculateModelSpaceTransforms(Skeleton::LOD lod)
	{
		i32 numTotalBones = m_skeleton->GetNumBones(Skeleton::LOD::High);
		i32 numRelevantBones = m_skeleton->GetNumBones(lod);
		m_modelSpaceTransforms.Resize(numTotalBones);

		m_modelSpaceTransforms[0] = m_parentSpaceTransforms[0];
		for (i32 boneIdx = 1; boneIdx < numRelevantBones; ++boneIdx)
		{
			i32 parentIdx = m_skeleton->GetParentBoneIndex(boneIdx);
			// childModel = childLocal * parentModel (parent always precedes child)
			m_modelSpaceTransforms[boneIdx] = m_parentSpaceTransforms[boneIdx] * m_modelSpaceTransforms[parentIdx];
		}
	}

	XForm Pose::GetModelSpaceTransform(i32 boneIdx) const
	{
		if (!m_modelSpaceTransforms.Empty())
		{
			return m_modelSpaceTransforms[boneIdx];
		}

		// Uncached: gather ancestors [immediate parent .. root], then compose from the
		// root downward so each step's right-hand operand is a true model-space transform.
		Array<i32> ancestors;
		i32        parentIdx = m_skeleton->GetParentBoneIndex(boneIdx);
		while (parentIdx != InvalidIndex)
		{
			ancestors.EmplaceBack(parentIdx);
			parentIdx = m_skeleton->GetParentBoneIndex(parentIdx);
		}

		XForm result = m_parentSpaceTransforms[boneIdx];
		if (!ancestors.Empty())
		{
			i32   arrayIdx = static_cast<i32>(ancestors.Size()) - 1;
			XForm parentModelSpace = m_parentSpaceTransforms[ancestors[arrayIdx--]]; // root
			for (; arrayIdx >= 0; --arrayIdx)
			{
				parentModelSpace = m_parentSpaceTransforms[ancestors[arrayIdx]] * parentModelSpace;
			}
			result = result * parentModelSpace;
		}

		return result;
	}
}
