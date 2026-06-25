#include "GraphDefinition.hpp"

namespace Skore::Anim
{
	GraphDefinition::~GraphDefinition()
	{
		for (GraphNode::Definition* def : m_nodeDefinitions)
		{
			delete def;
		}
		m_nodeDefinitions.Clear();
	}
}
