#pragma once

#include "SceneCommon.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	struct LayerSettings
	{
		enum
		{
			Name //String
		};
	};

	struct LayerSystemSettings
	{
		enum
		{
			Layers //SubObjectList
		};
	};

	class SK_API LayerSystem
	{
	public:
		static StringView GetLayerName(u8 layer);
		static void       SetLayerName(u8 layer, StringView name);
		static i32        FindLayerByName(StringView name);
	};
}
