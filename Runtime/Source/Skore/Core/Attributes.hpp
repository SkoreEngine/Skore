#pragma once
#include "Skore/Common.hpp"

namespace Skore
{
	struct UIIgnore {};

	struct UISliderProperty
	{
		f32      minValue{};
		f32      maxValue{};
		ConstChr format = nullptr;
	};

	struct UIFloatProperty
	{
		f32 minValue{};
		f32 maxValue{};
	};

	struct UIIntProperty
	{
		i32 minValue{};
		i32 maxValue{};
	};

	struct UIArrayProperty
	{
		bool canAdd = true;
		bool canRemove = true;
	};

	struct UIStringProperty
	{
		bool multiline = false;
	};

	struct UILayerMaskProperty {};

	struct VirtualMethod{};

	struct Iterable{};
}
