#pragma once

#include "GraphNode.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	class Skeleton;
	class AnimationClip;

	// The compiled, immutable description of a graph: a flat node-definition array plus the
	// per-node instance memory layout (offsets / total size / alignment) and the root node.
	// Definitions are shared across instances; per-character state lives in the GraphInstance.
	// For S2 this is hand-built in code via the builder helpers; the resource compiler fills
	// it at S4. Owns its node definitions. Spec §8.1.
	class SK_API GraphDefinition
	{
	public:
		GraphDefinition() = default;
		GraphDefinition(const GraphDefinition&) = delete;
		GraphDefinition& operator=(const GraphDefinition&) = delete;
		~GraphDefinition();

		bool            IsValid() const { return m_skeleton != nullptr && m_rootNodeIdx != static_cast<i16>(InvalidIndex); }
		const Skeleton* GetSkeleton() const { return m_skeleton; }

		// Builder (S2 hand-building) ------------------------------------------------------

		void SetSkeleton(const Skeleton* skeleton) { m_skeleton = skeleton; }

		// Create a node definition of type T and reserve its (aligned) slot in the instance
		// memory block. T must be a complete node type at the call site.
		template<typename T>
		typename T::Definition* CreateNodeDefinition()
		{
			auto* def = new typename T::Definition();
			def->m_nodeIdx = static_cast<i16>(m_nodeDefinitions.Size());
			m_nodeDefinitions.EmplaceBack(def);

			u32 alignment = static_cast<u32>(alignof(T));
			u32 misalignment = m_instanceRequiredMemory % alignment;
			u32 pad = (misalignment == 0) ? 0u : (alignment - misalignment);
			m_instanceNodeStartOffsets.EmplaceBack(m_instanceRequiredMemory + pad);
			m_instanceRequiredMemory += static_cast<u32>(sizeof(T)) + pad;
			if (alignment > m_instanceRequiredAlignment) m_instanceRequiredAlignment = alignment;
			return def;
		}

		i16  AddClipResource(const AnimationClip* clip) { m_resources.EmplaceBack(clip); return static_cast<i16>(m_resources.Size() - 1); }
		void AddPersistentNode(i16 nodeIdx) { m_persistentNodeIndices.EmplaceBack(nodeIdx); }
		void SetRootNodeIndex(i16 nodeIdx) { m_rootNodeIdx = nodeIdx; }

		// Data (public for S2 hand-building + GraphInstance access) ------------------------

		const Skeleton*               m_skeleton = nullptr;
		Array<i16>                    m_persistentNodeIndices;
		Array<u32>                    m_instanceNodeStartOffsets;
		u32                           m_instanceRequiredMemory = 0;
		u32                           m_instanceRequiredAlignment = static_cast<u32>(alignof(GraphNode*));
		i16                           m_rootNodeIdx = static_cast<i16>(InvalidIndex);
		Array<GraphNode::Definition*> m_nodeDefinitions; // owned (freed in dtor)
		Array<const AnimationClip*>   m_resources;
	};
}
