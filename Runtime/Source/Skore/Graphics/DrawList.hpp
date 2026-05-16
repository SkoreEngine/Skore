#pragma once

#include "Device.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class GPURenderPass;
	class GPUCommandBuffer;
	class GPUPipeline;
	class GPUBuffer;

	struct DrawListContext;

	namespace DrawList
	{
		SK_API DrawListContext* CreateContext();
		SK_API void             DestroyContext(DrawListContext* context);
		SK_API void             Flush(DrawListContext* context, GPURenderPass* renderPass, Extent extent, GPUCommandBuffer* cmd);

		SK_API void AddText(DrawListContext* context, RID font, const Vec2& position, f32 size, StringView text, const Color& color);
		SK_API void AddImage(DrawListContext* context, GPUTexture* texture, const Vec2& min, const Vec2& max, const Vec2& uvMin = Vec2(0.0, 0.0), const Vec2& uvMax = Vec2(1.0, 1.0), const Color& color = Color::WHITE);
		SK_API void AddRectFilled(DrawListContext* context, const Vec2& min, const Vec2& max, const Color& color);
		SK_API void AddRect(DrawListContext* context, const Vec2& min, const Vec2& max, const Color& color, f32 thickness = 1.0f);
		SK_API void PushScissorRect(DrawListContext* context, const Vec2& min, const Vec2& max);
		SK_API void PopScissorRect(DrawListContext* context);
		SK_API Vec2 MeasureText(RID font, f32 size, StringView text);
	}
}
