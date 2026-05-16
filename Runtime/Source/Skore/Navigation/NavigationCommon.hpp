#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	enum class NavMeshAreaType : u8
	{
		Walkable    = 0,
		NotWalkable = 1,
		Water       = 2,
		Grass       = 3
	};

	struct NavMeshBuildSettings
	{
		f32 cellSize           = 0.3f;
		f32 cellHeight         = 0.2f;
		f32 agentHeight        = 2.0f;
		f32 agentRadius        = 0.6f;
		f32 agentMaxClimb      = 0.9f;
		f32 agentMaxSlope      = 45.0f;
		f32 regionMinSize      = 8.0f;
		f32 regionMergeSize    = 20.0f;
		f32 edgeMaxLen         = 12.0f;
		f32 edgeMaxError       = 1.3f;
		i32 vertsPerPoly       = 6;
		f32 detailSampleDist   = 6.0f;
		f32 detailSampleMaxError = 1.0f;
	};

	struct NavMeshDebugVertex
	{
		Vec3 position;
		Vec3 color;
	};

	struct NavMeshPath
	{
		Array<Vec3> waypoints;
		bool isPartial = false;
	};
}
