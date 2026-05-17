#pragma once

#include "Skore/Core/Math.hpp"

namespace Skore
{
	class GPUFramebuffer;
	class GPUCommandBuffer;
	class GPURenderPass;
	class Scene;
	struct UIRendererContext;

	struct DrawUICursorState
	{
		Vec2 position = {};
		bool cursorDown = {};
		Vec2 mouseWheel = {};
	};

	class SK_API UIRenderer
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(UIRenderer);

		UIRenderer();
		~UIRenderer();

		void SetDebugModeEnabled(bool enabled);
		bool IsDebugModeEnabled() const;

		void DrawUI(Scene* scene, DrawUICursorState cursorState, GPURenderPass* renderPass, Extent extent, GPUCommandBuffer* cmd);
	private:
		char m_context[64] = {};
	};
}
