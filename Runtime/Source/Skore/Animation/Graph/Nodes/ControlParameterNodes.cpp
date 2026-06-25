#include "ControlParameterNodes.hpp"

namespace Skore::Anim
{
	void ControlParameterFloatNode::Definition::InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const
	{
		CreateNode<ControlParameterFloatNode>(context, options);
	}

	void ControlParameterFloatNode::GetValueInternal(GraphContext& context, void* pOutValue)
	{
		(void) context;
		*static_cast<f32*>(pOutValue) = m_value;
	}
}
