#pragma once

#include "Skore/Common.hpp"

namespace Skore::Anim
{
	// The typed values that flow through a graph's value nodes (pose is the special stream).
	enum class GraphValueType : u8
	{
		Unknown = 0,
		Bool,
		ID,
		Float,
		Vector, // 3D
		Target,
		BoneMask,
		Pose,
		Special, // custom graph pin types
	};
}
