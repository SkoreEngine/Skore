#pragma once

#include "AnimXForm.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	class Skeleton; // scene ECS component (Scene/Components/RenderComponents.hpp)
}

namespace Skore::Anim
{
	// Runtime skeleton: the canonical bone order + bind (reference) pose, built once from
	// the scene Skeleton component. Pose, Blender and the clip cache all key off this.
	// Bones are ordered parent-before-child, so a single linear pass computes model space.
	// A port of Esoterica's Animation::Skeleton (the parts the runtime needs). Spec §3.3.
	//
	// S1: single LOD (Low == High == all bones). The two-level LOD
	// (numBonesToSampleAtLowLOD) arrives only if LOD'd sampling is needed later.
	class SK_API Skeleton
	{
	public:
		enum class LOD : u8
		{
			Low = 0,
			High
		};

		// Build from the scene Skeleton component (captures its current bones as the bind
		// pose — call at OnStart, before any animation has mutated the bones).
		void BuildFrom(const Skore::Skeleton* sceneSkeleton);

		bool IsValid() const { return m_numBones > 0; }

		i32 GetNumBones() const { return m_numBones; }
		i32 GetNumBones(LOD lod) const { return (lod == LOD::Low) ? m_numBonesToSampleAtLowLOD : m_numBones; }

		i32               GetParentBoneIndex(i32 idx) const { return m_parentIndices[idx]; }
		const Array<i32>& GetParentBoneIndices() const { return m_parentIndices; }

		const Array<XForm>& GetParentSpaceReferencePose() const { return m_parentSpaceReferencePose; }
		const Array<XForm>& GetModelSpaceReferencePose() const { return m_modelSpaceReferencePose; }

		const Array<String>& GetBoneIDs() const { return m_boneIDs; }
		i32                  GetBoneIndex(const String& boneName) const; // InvalidIndex if absent

		// Bone masks live on the skeleton in Esoterica; they arrive with authoring (S6).
		u32 GetNumBoneMasks() const { return 0; }

	private:
		Array<String> m_boneIDs;
		Array<i32>    m_parentIndices;             // InvalidIndex (-1) == root
		Array<XForm>  m_parentSpaceReferencePose;  // bind pose, local
		Array<XForm>  m_modelSpaceReferencePose;   // bind pose, global (cached FK)
		i32           m_numBones = 0;
		i32           m_numBonesToSampleAtLowLOD = 0;
	};
}
