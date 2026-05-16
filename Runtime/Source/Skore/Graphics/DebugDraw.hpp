#pragma once
#include "Device.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore {
	class GPUCommandBuffer;
}

namespace Skore
{
	struct DebugDrawVertex
	{
		Vec3 position;
		u32  color = 0;
	};

	class SK_API DebugDraw final
	{
	public:
		void DrawLine(Vec3 start, Vec3 end, f32 thickness = 1.0, Color color = Color::RED);
		void DrawAABB(const AABB& aabb, f32 thickness = 1.0, Color color = Color::RED);
		void DrawFrustum(const Frustum& frustum, f32 thickness = 1.0, Color color = Color::RED);
		void Draw(GPUCommandBuffer* cmd);

		~DebugDraw();

	private:
		DebugDrawVertex* vertices = nullptr;
		u32*  indices = nullptr;
		u32   vertexCount = 0;
		u32   indexCount = 0;
		u32   vertexCapacity = 0;
		u32   indexCapacity = 0;

		GPUBuffer* vertexBuffer = nullptr;
		GPUBuffer* indexBuffer = nullptr;

		void Reserve(u32 reserveVertexCount, u32 reserveIndexCount);
	};
}
