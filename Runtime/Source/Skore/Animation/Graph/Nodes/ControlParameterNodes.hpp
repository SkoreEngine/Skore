#pragma once

#include "Skore/Animation/Graph/GraphNode.hpp"

namespace Skore::Anim
{
	class GraphInstance;

	// A control parameter: a float value node that holds an externally-set value (gameplay or
	// the editor preview drive it via GraphInstance::SetControlParameterFloat). It just returns
	// its stored value. Port of Esoterica's ControlParameterFloatNode. (Bool/ID/Vector/Target
	// control params + virtual params follow when needed; the name->index lookup awaits the
	// interned-string-id decision, R5.) Spec §5.4.
	class SK_API ControlParameterFloatNode final : public FloatValueNode
	{
		friend class GraphInstance;

	public:
		struct SK_API Definition final : public GraphNode::Definition
		{
			void InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const override;
		};

	private:
		void DirectlySetValue(f32 value) { m_value = value; }
		void GetValueInternal(GraphContext& context, void* pOutValue) override;
		void SetValueInternal(const void* pInValue) override { m_value = *static_cast<const f32*>(pInValue); }

		f32 m_value = 0.0f;
	};
}
