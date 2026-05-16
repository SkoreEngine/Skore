#pragma once
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	class SK_API XROrigin : public Component
	{
	public:
		SK_CLASS(XROrigin, Component);

		static void RegisterType(NativeReflectType<XROrigin>& type);
	};

	class SK_API XRNode : public Component
	{
	public:
		SK_CLASS(XRNode, Component);
		static void RegisterType(NativeReflectType<XRNode>& type);
	};
}
