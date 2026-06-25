#include "AnimationSkeleton.hpp"

#include "Skore/Scene/Components/RenderComponents.hpp"

namespace Skore::Anim
{
	void Skeleton::BuildFrom(const Skore::Skeleton* sceneSkeleton)
	{
		const Array<BoneNode>& bones = sceneSkeleton->bones;
		m_numBones = static_cast<i32>(bones.Size());
		m_numBonesToSampleAtLowLOD = m_numBones; // single LOD for now

		m_boneIDs.Resize(m_numBones);
		m_parentIndices.Resize(m_numBones);
		m_parentSpaceReferencePose.Resize(m_numBones);

		for (i32 i = 0; i < m_numBones; ++i)
		{
			m_boneIDs[i] = bones[i].name;
			m_parentIndices[i] = (bones[i].parentIndex == U32_MAX) ? InvalidIndex : static_cast<i32>(bones[i].parentIndex);

			XForm& x = m_parentSpaceReferencePose[i];
			x.m_rotation    = bones[i].rotation;
			x.m_translation = bones[i].position;
			x.m_scale       = bones[i].scale.x; // XForm is uniform-scale (see AnimXForm.hpp)
		}

		// Model-space reference pose via a single parent-before-child pass.
		m_modelSpaceReferencePose.Resize(m_numBones);
		for (i32 i = 0; i < m_numBones; ++i)
		{
			i32 parentIdx = m_parentIndices[i];
			if (parentIdx >= 0)
			{
				m_modelSpaceReferencePose[i] = m_parentSpaceReferencePose[i] * m_modelSpaceReferencePose[parentIdx];
			}
			else
			{
				m_modelSpaceReferencePose[i] = m_parentSpaceReferencePose[i];
			}
		}
	}

	i32 Skeleton::GetBoneIndex(const String& boneName) const
	{
		for (i32 i = 0; i < m_numBones; ++i)
		{
			if (m_boneIDs[i] == boneName) return i;
		}
		return InvalidIndex;
	}
}
