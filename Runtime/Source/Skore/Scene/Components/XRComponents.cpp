#include "Skore/Scene/Components/XRComponents.hpp"

#include "Skore/Core/Reflection.hpp"


namespace Skore
{
	void XROrigin::RegisterType(NativeReflectType<XROrigin>& type)
	{
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "XR"});
	}

	void XRNode::RegisterType(NativeReflectType<XRNode>& type)
	{
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "XR"});
	}
}