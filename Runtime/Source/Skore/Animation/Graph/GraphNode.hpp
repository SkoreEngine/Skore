#pragma once

#include "GraphValueType.hpp"
#include "GraphContext.hpp"
#include "SampledEvents.hpp"
#include "Skore/Animation/AnimXForm.hpp"
#include "Skore/Animation/SyncTrack.hpp"
#include "Skore/Core/Array.hpp"

#include <new> // placement new

namespace Skore::Anim
{
	class GraphNode;
	class Skeleton;
	class AnimationClip;

	//-------------------------------------------------------------------------
	// Instantiation
	//-------------------------------------------------------------------------

	enum class InstantiationOptions
	{
		CreateNode,        // actually create the node
		NodeAlreadyCreated // node already created by a derived class; just wire its ptrs
	};

	// Hands a node definition the placement-new slots + resources during instantiation.
	// NOTE: node ptrs are not guaranteed constructed yet — only wire them, don't access.
	struct InstantiationContext
	{
		template<typename T>
		void SetNodePtrFromIndex(i16 nodeIdx, T*& target) const
		{
			target = reinterpret_cast<T*>(m_nodePtrs[nodeIdx]);
		}

		template<typename T>
		void SetOptionalNodePtrFromIndex(i16 nodeIdx, T*& target) const
		{
			target = (nodeIdx == static_cast<i16>(InvalidIndex)) ? nullptr : reinterpret_cast<T*>(m_nodePtrs[nodeIdx]);
		}

		// S2 resource access is just the clip table; the full resource-slot / variation
		// system arrives with the compiler (S4).
		const AnimationClip* GetClipResource(i16 slotIdx) const
		{
			if (slotIdx == static_cast<i16>(InvalidIndex) || m_pResources == nullptr) return nullptr;
			return (*m_pResources)[slotIdx];
		}

		i16                                m_currentNodeIdx = static_cast<i16>(InvalidIndex);
		Array<GraphNode*>&                 m_nodePtrs;
		const Skeleton*                    m_pSkeleton = nullptr;
		const Array<const AnimationClip*>* m_pResources = nullptr;
		u64                                m_userID = 0;
	};

	//-------------------------------------------------------------------------
	// Graph Node
	//-------------------------------------------------------------------------

	class SK_API GraphNode
	{
	public:
		// Immutable, shared per-node settings stored in the GraphDefinition.
		struct Definition
		{
			Definition() = default;
			virtual ~Definition() = default;

			// Create the node instance (placement-new) and wire its child pointers.
			virtual void InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const = 0;

			i16 m_nodeIdx = static_cast<i16>(InvalidIndex);

		protected:
			template<typename T>
			T* CreateNode(const InstantiationContext& context, InstantiationOptions options) const
			{
				T* pNode = reinterpret_cast<T*>(context.m_nodePtrs[m_nodeIdx]);
				if (options == InstantiationOptions::CreateNode)
				{
					new (pNode) T();
					pNode->m_pDefinition = this;
				}
				return pNode;
			}
		};

	public:
		GraphNode() = default;
		virtual ~GraphNode() = default;

		virtual bool           IsValid() const { return true; }
		virtual GraphValueType GetValueType() const = 0;
		i16                    GetNodeIndex() const { return m_pDefinition->m_nodeIdx; }

		bool         IsInitialized() const { return m_initializationCount > 0; }
		virtual void Initialize(GraphContext& context);
		void         Shutdown(GraphContext& context);

		// Is this node active i.e. was it updated this frame.
		bool IsNodeActive(u32 updateID) const { return m_lastUpdateID == updateID; }
		bool WasUpdated(GraphContext& context) const { return IsNodeActive(context.m_updateID); }
		void MarkNodeActive(GraphContext& context) { m_lastUpdateID = context.m_updateID; }

	protected:
		virtual void InitializeInternal(GraphContext& context);
		virtual void ShutdownInternal(GraphContext& context);

		template<typename T>
		const typename T::Definition* GetDefinition() const
		{
			return reinterpret_cast<const typename T::Definition*>(m_pDefinition);
		}

	protected:
		const Definition* m_pDefinition = nullptr;
		u32               m_lastUpdateID = 0xFFFFFFFF;
		i32               m_initializationCount = 0;
	};

	//-------------------------------------------------------------------------
	// Pose Nodes
	//-------------------------------------------------------------------------

	// The lightweight result a pose node returns each frame: a task recipe index, a root
	// motion delta, and a span of sampled events.
	struct GraphPoseNodeResult
	{
		bool HasRegisteredTasks() const { return m_taskIdx != static_cast<i8>(InvalidIndex); }

		i8                m_taskIdx = static_cast<i8>(InvalidIndex);
		XForm             m_rootMotionDelta = XForm::Identity();
		SampledEventRange m_sampledEventRange;
	};

	class SK_API PoseNode : public GraphNode
	{
	public:
		virtual const SyncTrack& GetSyncTrack() const = 0;

		i32        GetLoopCount() const { return m_loopCount; }
		Percentage GetPreviousTime() const { return m_previousTime; }
		Percentage GetCurrentTime() const { return m_currentTime; }
		f32        GetDuration() const { return m_duration; }

		// Initialize with a specific start time.
		void         Initialize(GraphContext& context, const SyncTrackTime& initialTime);
		virtual void InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime);

		// If pUpdateRange is set this performs a synchronized update; otherwise it advances
		// by the frame delta.
		virtual GraphPoseNodeResult Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange = nullptr) = 0;

	private:
		void           Initialize(GraphContext& context) override final { Initialize(context, SyncTrackTime()); }
		void           InitializeInternal(GraphContext& context) override final { InitializeInternal(context, SyncTrackTime()); }
		GraphValueType GetValueType() const override final { return GraphValueType::Pose; }

	protected:
		i32        m_loopCount = 0;
		f32        m_duration = 0.0f;
		Percentage m_currentTime = 0.0f;  // clamped percentage over the duration
		Percentage m_previousTime = 0.0f; // clamped percentage over the duration
	};

	//-------------------------------------------------------------------------
	// Value Nodes
	//-------------------------------------------------------------------------

	class SK_API ValueNode : public GraphNode
	{
	public:
		template<typename T>
		T GetValue(GraphContext& context)
		{
			T value;
			GetValueInternal(context, &value);
			return value;
		}

		template<typename T>
		void SetValue(const T& value)
		{
			SetValueInternal(&value);
		}

	protected:
		virtual void GetValueInternal(GraphContext& context, void* pValue) = 0;
		virtual void SetValueInternal(const void* pValue) {}
	};

	class SK_API BoolValueNode : public ValueNode
	{
		GraphValueType GetValueType() const override final { return GraphValueType::Bool; }
	};

	class SK_API IDValueNode : public ValueNode
	{
		GraphValueType GetValueType() const override final { return GraphValueType::ID; }
	};

	class SK_API FloatValueNode : public ValueNode
	{
		GraphValueType GetValueType() const override final { return GraphValueType::Float; }
	};

	class SK_API VectorValueNode : public ValueNode
	{
		GraphValueType GetValueType() const override final { return GraphValueType::Vector; }
	};
}
