#include "Skore/Graphics/DebugDraw.hpp"

#include "Skore/App.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		//must match DebugDraw.raster
		constexpr u32 FlagEnd = 1 << 0;    //vertex anchors on the end point
		constexpr u32 FlagSide = 1 << 1;   //offset to the positive perpendicular side
		constexpr u32 FlagExtend = 1 << 2; //also offset along the line direction (used for points)
	}

	void DebugDraw::BuildOrthonormalBasis(const Vec3& normal, Vec3& tangent, Vec3& bitangent)
	{
		Vec3 up = Math::Abs(normal.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
		tangent = Vec3::Normalize(Vec3::Cross(up, normal));
		bitangent = Vec3::Cross(normal, tangent);
	}

	DebugDraw::LineBatch& DebugDraw::GetBatch(bool depthTest)
	{
		return batches[depthTest ? 0 : 1];
	}

	void DebugDraw::Reserve(LineBatch& batch, u32 reserveVertexCount, u32 reserveIndexCount)
	{
		if (batch.vertexCount + reserveVertexCount > batch.vertexCapacity)
		{
			batch.vertexCapacity = Math::Max((batch.vertexCount + reserveVertexCount) * 2, 256u);
			batch.vertices = ReallocElements<DebugDrawVertex>(batch.vertices, batch.vertexCapacity);
		}

		if (batch.indexCount + reserveIndexCount > batch.indexCapacity)
		{
			batch.indexCapacity = Math::Max((batch.indexCount + reserveIndexCount) * 2, 384u);
			batch.indices = ReallocElements<u32>(batch.indices, batch.indexCapacity);
		}
	}

	void DebugDraw::AddLine(LineBatch& batch, Vec3 start, Vec3 end, f32 thickness, u32 color)
	{
		Reserve(batch, 4, 6);

		u32 firstVertex = batch.vertexCount;

		batch.vertices[batch.vertexCount++] = DebugDrawVertex{start, color, end, thickness, 0};
		batch.vertices[batch.vertexCount++] = DebugDrawVertex{start, color, end, thickness, FlagSide};
		batch.vertices[batch.vertexCount++] = DebugDrawVertex{start, color, end, thickness, FlagEnd | FlagSide};
		batch.vertices[batch.vertexCount++] = DebugDrawVertex{start, color, end, thickness, FlagEnd};

		batch.indices[batch.indexCount++] = firstVertex + 0;
		batch.indices[batch.indexCount++] = firstVertex + 1;
		batch.indices[batch.indexCount++] = firstVertex + 2;
		batch.indices[batch.indexCount++] = firstVertex + 0;
		batch.indices[batch.indexCount++] = firstVertex + 2;
		batch.indices[batch.indexCount++] = firstVertex + 3;
	}

	void DebugDraw::AddPoint(LineBatch& batch, Vec3 position, f32 size, u32 color)
	{
		Reserve(batch, 4, 6);

		u32 firstVertex = batch.vertexCount;

		batch.vertices[batch.vertexCount++] = DebugDrawVertex{position, color, position, size, FlagExtend};
		batch.vertices[batch.vertexCount++] = DebugDrawVertex{position, color, position, size, FlagExtend | FlagSide};
		batch.vertices[batch.vertexCount++] = DebugDrawVertex{position, color, position, size, FlagExtend | FlagEnd | FlagSide};
		batch.vertices[batch.vertexCount++] = DebugDrawVertex{position, color, position, size, FlagExtend | FlagEnd};

		batch.indices[batch.indexCount++] = firstVertex + 0;
		batch.indices[batch.indexCount++] = firstVertex + 1;
		batch.indices[batch.indexCount++] = firstVertex + 2;
		batch.indices[batch.indexCount++] = firstVertex + 0;
		batch.indices[batch.indexCount++] = firstVertex + 2;
		batch.indices[batch.indexCount++] = firstVertex + 3;
	}

	void DebugDraw::DrawLine(Vec3 start, Vec3 end, f32 thickness, Color color, bool depthTest)
	{
		AddLine(GetBatch(depthTest), start, end, thickness, Color::ToU32(color));
	}

	void DebugDraw::DrawLinePath(Span<Vec3> points, bool closed, f32 thickness, Color color, bool depthTest)
	{
		if (points.Size() < 2)
		{
			return;
		}

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		for (usize i = 0; i < points.Size() - 1; ++i)
		{
			AddLine(batch, points[i], points[i + 1], thickness, colorU32);
		}

		if (closed)
		{
			AddLine(batch, points[points.Size() - 1], points[0], thickness, colorU32);
		}
	}

	void DebugDraw::DrawArrow(Vec3 start, Vec3 end, f32 thickness, Color color, bool depthTest, f32 headSize)
	{
		f32 length = Vec3::Distance(start, end);
		if (length < 0.0001f)
		{
			return;
		}

		Vec3 direction = (end - start) / length;

		f32 headLength = headSize > 0.0f ? headSize : Math::Min(length * 0.25f, 0.5f);
		headLength = Math::Min(headLength, length);
		f32 headRadius = headLength * 0.4f;

		Vec3 headBase = end - direction * headLength;

		Vec3 tangent, bitangent;
		BuildOrthonormalBasis(direction, tangent, bitangent);

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		AddLine(batch, start, headBase, thickness, colorU32);
		AddLine(batch, headBase + tangent * headRadius, end, thickness, colorU32);
		AddLine(batch, headBase - tangent * headRadius, end, thickness, colorU32);
		AddLine(batch, headBase + bitangent * headRadius, end, thickness, colorU32);
		AddLine(batch, headBase - bitangent * headRadius, end, thickness, colorU32);

		DrawCircle(headBase, headRadius, direction, thickness, color, depthTest, 16);
	}

	void DebugDraw::DrawAABB(const AABB& aabb, f32 thickness, Color color, bool depthTest)
	{
		DrawBox(Mat4{1.0}, aabb, thickness, color, depthTest);
	}

	void DebugDraw::DrawBox(const Mat4& transform, const AABB& box, f32 thickness, Color color, bool depthTest)
	{
		Vec3 min = box.min;
		Vec3 max = box.max;

		Vec3 corners[8] = {
			Vec3(min.x, min.y, min.z),
			Vec3(max.x, min.y, min.z),
			Vec3(max.x, min.y, max.z),
			Vec3(min.x, min.y, max.z),
			Vec3(min.x, max.y, min.z),
			Vec3(max.x, max.y, min.z),
			Vec3(max.x, max.y, max.z),
			Vec3(min.x, max.y, max.z),
		};

		for (Vec3& corner : corners)
		{
			Vec4 transformed = transform * Vec4(corner, 1.0f);
			corner = Vec3(transformed.x, transformed.y, transformed.z);
		}

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		constexpr u32 edges[12][2] = {
			{0, 1}, {1, 2}, {2, 3}, {3, 0}, //bottom
			{4, 5}, {5, 6}, {6, 7}, {7, 4}, //top
			{0, 4}, {1, 5}, {2, 6}, {3, 7}, //verticals
		};

		for (const u32* edge : edges)
		{
			AddLine(batch, corners[edge[0]], corners[edge[1]], thickness, colorU32);
		}
	}

	void DebugDraw::DrawBox(const Mat4& transform, Vec3 halfExtents, f32 thickness, Color color, bool depthTest)
	{
		DrawBox(transform, AABB{-halfExtents, halfExtents}, thickness, color, depthTest);
	}

	void DebugDraw::DrawFrustum(const Frustum& frustum, f32 thickness, Color color, bool depthTest)
	{
		auto Intersect3 = [](const Plane& p1, const Plane& p2, const Plane& p3) -> Vec3
		{
			const Vec3 c23 = Vec3::Cross(p2.normal, p3.normal);
			const Vec3 c31 = Vec3::Cross(p3.normal, p1.normal);
			const Vec3 c12 = Vec3::Cross(p1.normal, p2.normal);
			const f32 denom = Vec3::Dot(p1.normal, c23);
			if (fabs(denom) < 1e-6f)
			{
				return Vec3(0.0f);
			}
			const Vec3 numerator = (-p1.distance) * c23 + (-p2.distance) * c31 + (-p3.distance) * c12;
			return numerator / denom;
		};

		const Plane& n = frustum.nearFace;
		const Plane& f = frustum.farFace;
		const Plane& l = frustum.leftFace;
		const Plane& r = frustum.rightFace;
		const Plane& t = frustum.topFace;
		const Plane& b = frustum.bottomFace;

		// Near plane corners
		Vec3 ntl = Intersect3(n, t, l);
		Vec3 ntr = Intersect3(n, t, r);
		Vec3 nbr = Intersect3(n, b, r);
		Vec3 nbl = Intersect3(n, b, l);

		// Far plane corners
		Vec3 ftl = Intersect3(f, t, l);
		Vec3 ftr = Intersect3(f, t, r);
		Vec3 fbr = Intersect3(f, b, r);
		Vec3 fbl = Intersect3(f, b, l);

		// Draw near plane rectangle
		DrawLine(ntl, ntr, thickness, color, depthTest);
		DrawLine(ntr, nbr, thickness, color, depthTest);
		DrawLine(nbr, nbl, thickness, color, depthTest);
		DrawLine(nbl, ntl, thickness, color, depthTest);

		// Draw far plane rectangle
		DrawLine(ftl, ftr, thickness, color, depthTest);
		DrawLine(ftr, fbr, thickness, color, depthTest);
		DrawLine(fbr, fbl, thickness, color, depthTest);
		DrawLine(fbl, ftl, thickness, color, depthTest);

		// Connect near and far planes
		DrawLine(ntl, ftl, thickness, color, depthTest);
		DrawLine(ntr, ftr, thickness, color, depthTest);
		DrawLine(nbr, fbr, thickness, color, depthTest);
		DrawLine(nbl, fbl, thickness, color, depthTest);
	}

	void DebugDraw::DrawCameraFrustum(const Mat4& worldTransform, f32 fovYDegrees, f32 aspect, f32 nearClip, f32 farClip, f32 thickness, Color color, bool depthTest)
	{
		Vec3 position = Mat4::GetTranslation(worldTransform);
		Vec3 right = Vec3::Normalize(Vec3(worldTransform[0][0], worldTransform[0][1], worldTransform[0][2]));
		Vec3 up = Vec3::Normalize(Vec3(worldTransform[1][0], worldTransform[1][1], worldTransform[1][2]));
		Vec3 forward = Vec3::Normalize(Mat4::GetForwardVector(worldTransform));

		f32 tanHalfFov = tanf(Math::Radians(fovYDegrees) * 0.5f);

		f32 nearHeight = tanHalfFov * nearClip;
		f32 nearWidth = nearHeight * aspect;
		f32 farHeight = tanHalfFov * farClip;
		f32 farWidth = farHeight * aspect;

		Vec3 nearCenter = position + forward * nearClip;
		Vec3 farCenter = position + forward * farClip;

		Vec3 ntl = nearCenter + up * nearHeight - right * nearWidth;
		Vec3 ntr = nearCenter + up * nearHeight + right * nearWidth;
		Vec3 nbr = nearCenter - up * nearHeight + right * nearWidth;
		Vec3 nbl = nearCenter - up * nearHeight - right * nearWidth;

		Vec3 ftl = farCenter + up * farHeight - right * farWidth;
		Vec3 ftr = farCenter + up * farHeight + right * farWidth;
		Vec3 fbr = farCenter - up * farHeight + right * farWidth;
		Vec3 fbl = farCenter - up * farHeight - right * farWidth;

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		AddLine(batch, ntl, ntr, thickness, colorU32);
		AddLine(batch, ntr, nbr, thickness, colorU32);
		AddLine(batch, nbr, nbl, thickness, colorU32);
		AddLine(batch, nbl, ntl, thickness, colorU32);

		AddLine(batch, ftl, ftr, thickness, colorU32);
		AddLine(batch, ftr, fbr, thickness, colorU32);
		AddLine(batch, fbr, fbl, thickness, colorU32);
		AddLine(batch, fbl, ftl, thickness, colorU32);

		AddLine(batch, ntl, ftl, thickness, colorU32);
		AddLine(batch, ntr, ftr, thickness, colorU32);
		AddLine(batch, nbr, fbr, thickness, colorU32);
		AddLine(batch, nbl, fbl, thickness, colorU32);

		//up direction marker above the near plane
		f32 markerSize = nearHeight * 0.4f;
		Vec3 markerBase = nearCenter + up * (nearHeight * 1.1f);
		AddLine(batch, markerBase - right * markerSize, markerBase + right * markerSize, thickness, colorU32);
		AddLine(batch, markerBase - right * markerSize, markerBase + up * markerSize, thickness, colorU32);
		AddLine(batch, markerBase + right * markerSize, markerBase + up * markerSize, thickness, colorU32);
	}

	void DebugDraw::DrawCircle(Vec3 center, f32 radius, Vec3 normal, f32 thickness, Color color, bool depthTest, u32 segments)
	{
		if (segments < 3)
		{
			segments = 3;
		}

		Vec3 tangent, bitangent;
		BuildOrthonormalBasis(Vec3::Normalize(normal), tangent, bitangent);

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		Vec3 prev = center + tangent * radius;
		for (u32 i = 1; i <= segments; ++i)
		{
			f32 angle = (f32)i / (f32)segments * TwoPI;
			Vec3 point = center + (tangent * Math::Cos(angle) + bitangent * Math::Sin(angle)) * radius;
			AddLine(batch, prev, point, thickness, colorU32);
			prev = point;
		}
	}

	void DebugDraw::DrawSphere(Vec3 center, f32 radius, f32 thickness, Color color, bool depthTest, u32 segments)
	{
		DrawCircle(center, radius, Vec3::AxisX(), thickness, color, depthTest, segments);
		DrawCircle(center, radius, Vec3::AxisY(), thickness, color, depthTest, segments);
		DrawCircle(center, radius, Vec3::AxisZ(), thickness, color, depthTest, segments);
	}

	void DebugDraw::DrawCapsule(const Mat4& transform, f32 radius, f32 height, f32 thickness, Color color, bool depthTest)
	{
		auto transformPoint = [&](Vec3 point) -> Vec3
		{
			Vec4 transformed = transform * Vec4(point, 1.0f);
			return Vec3(transformed.x, transformed.y, transformed.z);
		};

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		f32 halfHeight = height * 0.5f;
		constexpr u32 segments = 32;
		constexpr u32 arcSegments = 16;

		//rings at the ends of the cylindrical section
		for (i32 side = -1; side <= 1; side += 2)
		{
			f32 y = halfHeight * (f32)side;
			Vec3 prev = transformPoint(Vec3(radius, y, 0));
			for (u32 i = 1; i <= segments; ++i)
			{
				f32 angle = (f32)i / (f32)segments * TwoPI;
				Vec3 point = transformPoint(Vec3(Math::Cos(angle) * radius, y, Math::Sin(angle) * radius));
				AddLine(batch, prev, point, thickness, colorU32);
				prev = point;
			}
		}

		//vertical connection lines
		AddLine(batch, transformPoint(Vec3(radius, -halfHeight, 0)), transformPoint(Vec3(radius, halfHeight, 0)), thickness, colorU32);
		AddLine(batch, transformPoint(Vec3(-radius, -halfHeight, 0)), transformPoint(Vec3(-radius, halfHeight, 0)), thickness, colorU32);
		AddLine(batch, transformPoint(Vec3(0, -halfHeight, radius)), transformPoint(Vec3(0, halfHeight, radius)), thickness, colorU32);
		AddLine(batch, transformPoint(Vec3(0, -halfHeight, -radius)), transformPoint(Vec3(0, halfHeight, -radius)), thickness, colorU32);

		//hemisphere arcs (XY and ZY planes, top and bottom)
		for (i32 side = -1; side <= 1; side += 2)
		{
			f32 y = halfHeight * (f32)side;

			Vec3 prevXY = transformPoint(Vec3(radius, y, 0));
			Vec3 prevZY = transformPoint(Vec3(0, y, radius));

			for (u32 i = 1; i <= arcSegments; ++i)
			{
				f32 angle = (f32)i / (f32)arcSegments * PI;
				f32 cosAngle = Math::Cos(angle);
				f32 sinAngle = Math::Sin(angle) * (f32)side;

				Vec3 pointXY = transformPoint(Vec3(cosAngle * radius, y + sinAngle * radius, 0));
				AddLine(batch, prevXY, pointXY, thickness, colorU32);
				prevXY = pointXY;

				Vec3 pointZY = transformPoint(Vec3(0, y + sinAngle * radius, cosAngle * radius));
				AddLine(batch, prevZY, pointZY, thickness, colorU32);
				prevZY = pointZY;
			}
		}
	}

	void DebugDraw::DrawCylinder(const Mat4& transform, f32 radius, f32 height, f32 thickness, Color color, bool depthTest)
	{
		auto transformPoint = [&](Vec3 point) -> Vec3
		{
			Vec4 transformed = transform * Vec4(point, 1.0f);
			return Vec3(transformed.x, transformed.y, transformed.z);
		};

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		f32 halfHeight = height * 0.5f;
		constexpr u32 segments = 32;

		for (i32 side = -1; side <= 1; side += 2)
		{
			f32 y = halfHeight * (f32)side;
			Vec3 prev = transformPoint(Vec3(radius, y, 0));
			for (u32 i = 1; i <= segments; ++i)
			{
				f32 angle = (f32)i / (f32)segments * TwoPI;
				Vec3 point = transformPoint(Vec3(Math::Cos(angle) * radius, y, Math::Sin(angle) * radius));
				AddLine(batch, prev, point, thickness, colorU32);
				prev = point;
			}
		}

		AddLine(batch, transformPoint(Vec3(radius, -halfHeight, 0)), transformPoint(Vec3(radius, halfHeight, 0)), thickness, colorU32);
		AddLine(batch, transformPoint(Vec3(-radius, -halfHeight, 0)), transformPoint(Vec3(-radius, halfHeight, 0)), thickness, colorU32);
		AddLine(batch, transformPoint(Vec3(0, -halfHeight, radius)), transformPoint(Vec3(0, halfHeight, radius)), thickness, colorU32);
		AddLine(batch, transformPoint(Vec3(0, -halfHeight, -radius)), transformPoint(Vec3(0, halfHeight, -radius)), thickness, colorU32);
	}

	void DebugDraw::DrawCone(Vec3 apex, Vec3 direction, f32 length, f32 angleRadians, f32 thickness, Color color, bool depthTest)
	{
		direction = Vec3::Normalize(direction);

		f32 baseRadius = tanf(angleRadians) * length;
		Vec3 baseCenter = apex + direction * length;

		Vec3 tangent, bitangent;
		BuildOrthonormalBasis(direction, tangent, bitangent);

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		AddLine(batch, apex, baseCenter + tangent * baseRadius, thickness, colorU32);
		AddLine(batch, apex, baseCenter - tangent * baseRadius, thickness, colorU32);
		AddLine(batch, apex, baseCenter + bitangent * baseRadius, thickness, colorU32);
		AddLine(batch, apex, baseCenter - bitangent * baseRadius, thickness, colorU32);

		DrawCircle(baseCenter, baseRadius, direction, thickness, color, depthTest);
	}

	void DebugDraw::DrawPlane(Vec3 center, Vec3 normal, Vec2 halfExtents, f32 thickness, Color color, bool depthTest)
	{
		normal = Vec3::Normalize(normal);

		Vec3 tangent, bitangent;
		BuildOrthonormalBasis(normal, tangent, bitangent);

		Vec3 c0 = center + tangent * halfExtents.x + bitangent * halfExtents.y;
		Vec3 c1 = center - tangent * halfExtents.x + bitangent * halfExtents.y;
		Vec3 c2 = center - tangent * halfExtents.x - bitangent * halfExtents.y;
		Vec3 c3 = center + tangent * halfExtents.x - bitangent * halfExtents.y;

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		AddLine(batch, c0, c1, thickness, colorU32);
		AddLine(batch, c1, c2, thickness, colorU32);
		AddLine(batch, c2, c3, thickness, colorU32);
		AddLine(batch, c3, c0, thickness, colorU32);

		AddLine(batch, c0, c2, thickness, colorU32);
		AddLine(batch, c1, c3, thickness, colorU32);

		DrawArrow(center, center + normal * Math::Min(halfExtents.x, halfExtents.y), thickness, color, depthTest);
	}

	void DebugDraw::DrawGrid(const Mat4& transform, u32 cellsX, u32 cellsZ, f32 cellSize, f32 thickness, Color color, bool depthTest)
	{
		auto transformPoint = [&](Vec3 point) -> Vec3
		{
			Vec4 transformed = transform * Vec4(point, 1.0f);
			return Vec3(transformed.x, transformed.y, transformed.z);
		};

		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		f32 halfX = (f32)cellsX * cellSize * 0.5f;
		f32 halfZ = (f32)cellsZ * cellSize * 0.5f;

		for (u32 i = 0; i <= cellsX; ++i)
		{
			f32 x = -halfX + (f32)i * cellSize;
			AddLine(batch, transformPoint(Vec3(x, 0, -halfZ)), transformPoint(Vec3(x, 0, halfZ)), thickness, colorU32);
		}

		for (u32 i = 0; i <= cellsZ; ++i)
		{
			f32 z = -halfZ + (f32)i * cellSize;
			AddLine(batch, transformPoint(Vec3(-halfX, 0, z)), transformPoint(Vec3(halfX, 0, z)), thickness, colorU32);
		}
	}

	void DebugDraw::DrawPoint(Vec3 position, f32 size, Color color, bool depthTest)
	{
		AddPoint(GetBatch(depthTest), position, size, Color::ToU32(color));
	}

	void DebugDraw::DrawPoints(Span<Vec3> points, f32 size, Color color, bool depthTest)
	{
		LineBatch& batch = GetBatch(depthTest);
		u32 colorU32 = Color::ToU32(color);

		for (const Vec3& point : points)
		{
			AddPoint(batch, point, size, colorU32);
		}
	}

	void DebugDraw::DrawPosition(Vec3 position, f32 size, f32 thickness, bool depthTest)
	{
		LineBatch& batch = GetBatch(depthTest);

		AddLine(batch, position - Vec3(size, 0, 0), position + Vec3(size, 0, 0), thickness, Color::ToU32(Color::RED));
		AddLine(batch, position - Vec3(0, size, 0), position + Vec3(0, size, 0), thickness, Color::ToU32(Color::GREEN));
		AddLine(batch, position - Vec3(0, 0, size), position + Vec3(0, 0, size), thickness, Color::ToU32(Color::BLUE));
	}

	void DebugDraw::DrawAxes(const Mat4& transform, f32 size, f32 thickness, bool depthTest)
	{
		Vec3 origin = Mat4::GetTranslation(transform);

		Vec3 axisX = Vec3::Normalize(Vec3(transform[0][0], transform[0][1], transform[0][2]));
		Vec3 axisY = Vec3::Normalize(Vec3(transform[1][0], transform[1][1], transform[1][2]));
		Vec3 axisZ = Vec3::Normalize(Vec3(transform[2][0], transform[2][1], transform[2][2]));

		DrawArrow(origin, origin + axisX * size, thickness, Color::RED, depthTest);
		DrawArrow(origin, origin + axisY * size, thickness, Color::GREEN, depthTest);
		DrawArrow(origin, origin + axisZ * size, thickness, Color::BLUE, depthTest);
	}

	void DebugDraw::Flush(GPUCommandBuffer* cmd, GPURenderPass* renderPass, const Mat4& viewProjection, Extent viewportSize)
	{
		if (batches[0].indexCount == 0 && batches[1].indexCount == 0)
		{
			return;
		}

		if (cachedRenderPass != renderPass || !depthTestPipeline)
		{
			DestroyPipelines();

			GraphicsPipelineDesc pipelineDesc = {
				.shader = Resources::FindByPath("Skore://Shaders/DebugDraw.raster"),
				.depthStencilState = {
					.depthTestEnable = true,
					.depthWriteEnable = false,
					.depthCompareOp = CompareOp::Greater //reverse-Z
				},
				.blendStates = {
					BlendStateDesc{
						.blendEnable = true
					}
				},
				.renderPass = renderPass,
				.vertexInputStride = sizeof(DebugDrawVertex)
			};
			depthTestPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);

			pipelineDesc.depthStencilState.depthTestEnable = false;
			overlayPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);

			cachedRenderPass = renderPass;
		}

		struct DebugDrawPushConstants
		{
			Mat4 viewProjection;
			Vec2 screenSize;
			Vec2 pad;
		};

		DebugDrawPushConstants pushConstants;
		pushConstants.viewProjection = viewProjection;
		pushConstants.screenSize = Vec2{(f32)viewportSize.width, (f32)viewportSize.height};
		pushConstants.pad = Vec2{0, 0};

		u32 slot = static_cast<u32>(App::Frame() % SK_FRAMES_IN_FLIGHT);

		FlushBatch(batches[0], depthTestPipeline, cmd, slot, &pushConstants, (u32)sizeof(DebugDrawPushConstants));
		FlushBatch(batches[1], overlayPipeline, cmd, slot, &pushConstants, (u32)sizeof(DebugDrawPushConstants));
	}

	void DebugDraw::FlushBatch(LineBatch& batch, GPUPipeline* pipeline, GPUCommandBuffer* cmd, u32 slot, const void* pushConstants, u32 pushConstantsSize)
	{
		if (batch.indexCount == 0)
		{
			return;
		}

		GPUBuffer*& vertexBuffer = batch.vertexBuffers[slot];
		if (vertexBuffer == nullptr || vertexBuffer->GetDesc().size < batch.vertexCount * sizeof(DebugDrawVertex))
		{
			if (vertexBuffer) vertexBuffer->Destroy();
			vertexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = batch.vertexCapacity * sizeof(DebugDrawVertex),
				.usage = ResourceUsage::VertexBuffer,
				.hostVisible = true,
				.persistentMapped = true
			});
		}

		GPUBuffer*& indexBuffer = batch.indexBuffers[slot];
		if (indexBuffer == nullptr || indexBuffer->GetDesc().size < batch.indexCount * sizeof(u32))
		{
			if (indexBuffer) indexBuffer->Destroy();
			indexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = batch.indexCapacity * sizeof(u32),
				.usage = ResourceUsage::IndexBuffer,
				.hostVisible = true,
				.persistentMapped = true
			});
		}

		memcpy(vertexBuffer->GetMappedData(), batch.vertices, batch.vertexCount * sizeof(DebugDrawVertex));
		memcpy(indexBuffer->GetMappedData(), batch.indices, batch.indexCount * sizeof(u32));

		cmd->BindPipeline(pipeline);
		cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, pushConstantsSize, pushConstants);
		cmd->BindVertexBuffer(0, vertexBuffer, 0);
		cmd->BindIndexBuffer(indexBuffer, 0, IndexType::Uint32);
		cmd->DrawIndexed(batch.indexCount, 1, 0, 0, 0);

		batch.vertexCount = 0;
		batch.indexCount = 0;
	}

	void DebugDraw::DestroyPipelines()
	{
		if (depthTestPipeline)
		{
			depthTestPipeline->Destroy();
			depthTestPipeline = nullptr;
		}

		if (overlayPipeline)
		{
			overlayPipeline->Destroy();
			overlayPipeline = nullptr;
		}
	}

	DebugDraw::~DebugDraw()
	{
		DestroyPipelines();

		for (LineBatch& batch : batches)
		{
			for (GPUBuffer* buffer : batch.vertexBuffers)
			{
				if (buffer) buffer->Destroy();
			}

			for (GPUBuffer* buffer : batch.indexBuffers)
			{
				if (buffer) buffer->Destroy();
			}

			if (batch.vertices) MemFree(batch.vertices);
			if (batch.indices) MemFree(batch.indices);
		}
	}
}
