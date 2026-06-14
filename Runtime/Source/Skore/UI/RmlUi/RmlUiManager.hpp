#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"

namespace Rml
{
	class Context;
}

namespace Skore
{
	class GPUCommandBuffer;
	class GPUTexture;
	class RenderInterfaceSkore;

	struct SK_API RmlUiManager
	{
		static RenderInterfaceSkore* GetRenderInterface();
		static void                  RegisterContext(Rml::Context* context);
		static void                  UnregisterContext(Rml::Context* context);
		static void                  SetContextInputTransform(Rml::Context* context, Vec2 offset, f32 scale);
	};
}
