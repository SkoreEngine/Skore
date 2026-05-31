#pragma once

#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"

namespace Skore
{
	// Concrete cascade-shadow pass instance. All rendering logic now lives in
	// CascadeShadowPassBase, which builds shadow pipelines, dispatches the GPU
	// culling compute shader, and issues indirect draws per cascade.
	struct CascadeShadowPass : CascadeShadowPassBase
	{
		SK_CLASS(CascadeShadowPass, CascadeShadowPassBase);
	};
}
