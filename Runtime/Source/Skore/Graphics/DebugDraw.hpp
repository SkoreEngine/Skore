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
		Vec3 position;          //line start (world space)
		u32  color = 0;
		Vec3 endPosition;       //line end (world space)
		f32  thickness = 1.0f;  //pixels
		u32  flags = 0;
	};

	class SK_API DebugDraw final
	{
	public:
		void DrawLine(Vec3 start, Vec3 end, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawLinePath(Span<Vec3> points, bool closed = false, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawArrow(Vec3 start, Vec3 end, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true, f32 headSize = 0.0f);
		void DrawAABB(const AABB& aabb, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawBox(const Mat4& transform, const AABB& box, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawBox(const Mat4& transform, Vec3 halfExtents, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawFrustum(const Frustum& frustum, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawCameraFrustum(const Mat4& worldTransform, f32 fovYDegrees, f32 aspect, f32 nearClip, f32 farClip, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawCircle(Vec3 center, f32 radius, Vec3 normal, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true, u32 segments = 32);
		void DrawSphere(Vec3 center, f32 radius, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true, u32 segments = 32);
		void DrawCapsule(const Mat4& transform, f32 radius, f32 height, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawCylinder(const Mat4& transform, f32 radius, f32 height, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawCone(Vec3 apex, Vec3 direction, f32 length, f32 angleRadians, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawPlane(Vec3 center, Vec3 normal, Vec2 halfExtents, f32 thickness = 1.0f, Color color = Color::RED, bool depthTest = true);
		void DrawGrid(const Mat4& transform, u32 cellsX, u32 cellsZ, f32 cellSize = 1.0f, f32 thickness = 1.0f, Color color = Color::GREY, bool depthTest = true);
		void DrawPoint(Vec3 position, f32 size = 5.0f, Color color = Color::RED, bool depthTest = true);
		void DrawPoints(Span<Vec3> points, f32 size = 5.0f, Color color = Color::RED, bool depthTest = true);

		//3 crossing axis lines through position, colored X=red Y=green Z=blue
		void DrawPosition(Vec3 position, f32 size = 0.5f, f32 thickness = 1.0f, bool depthTest = true);

		//oriented basis arrows of a transform, colored X=red Y=green Z=blue
		void DrawAxes(const Mat4& transform, f32 size = 1.0f, f32 thickness = 1.0f, bool depthTest = true);

		//records everything queued since the last flush and resets. must be called inside an active render pass.
		void Flush(GPUCommandBuffer* cmd, GPURenderPass* renderPass, const Mat4& viewProjection, Extent viewportSize);

		static void BuildOrthonormalBasis(const Vec3& normal, Vec3& tangent, Vec3& bitangent);

		~DebugDraw();

	private:
		struct LineBatch
		{
			DebugDrawVertex* vertices = nullptr;
			u32*             indices = nullptr;
			u32              vertexCount = 0;
			u32              indexCount = 0;
			u32              vertexCapacity = 0;
			u32              indexCapacity = 0;

			GPUBuffer* vertexBuffers[SK_FRAMES_IN_FLIGHT] = {};
			GPUBuffer* indexBuffers[SK_FRAMES_IN_FLIGHT] = {};
		};

		LineBatch batches[2]; //0 = depth tested, 1 = overlay

		GPURenderPass* cachedRenderPass = nullptr;
		GPUPipeline*   depthTestPipeline = nullptr;
		GPUPipeline*   overlayPipeline = nullptr;

		LineBatch& GetBatch(bool depthTest);
		void       Reserve(LineBatch& batch, u32 reserveVertexCount, u32 reserveIndexCount);
		void       AddLine(LineBatch& batch, Vec3 start, Vec3 end, f32 thickness, u32 color);
		void       AddPoint(LineBatch& batch, Vec3 position, f32 size, u32 color);
		void       FlushBatch(LineBatch& batch, GPUPipeline* pipeline, GPUCommandBuffer* cmd, u32 slot, const void* pushConstants, u32 pushConstantsSize);
		void       DestroyPipelines();
	};
}
