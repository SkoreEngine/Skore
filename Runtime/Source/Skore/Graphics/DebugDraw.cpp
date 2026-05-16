#include "Skore/Graphics/DebugDraw.hpp"

#include "Skore/Graphics/Graphics.hpp"


namespace Skore
{
	void DebugDraw::DrawLine(Vec3 start, Vec3 end, f32 thickness, Color color)
	{
		Reserve(4, 6);

		thickness = thickness / 100.f;

		Vec3 direction = end - start;

		Vec3 up = Vec3(0, 1, 0);
		if (abs(direction.y) > 0.99f)
		{
			up = Vec3(1, 0, 0);
		}

		Vec3 perpendicular = Vec3::Normalize(Vec3::Cross(direction, up)) * (thickness * 0.5f);

		u32 firstVertex = vertexCount;

		vertices[vertexCount++] = DebugDrawVertex(start - perpendicular, Color::ToU32(color));
		vertices[vertexCount++] = DebugDrawVertex(start + perpendicular, Color::ToU32(color));
		vertices[vertexCount++] = DebugDrawVertex(end + perpendicular, Color::ToU32(color));
		vertices[vertexCount++] = DebugDrawVertex(end - perpendicular, Color::ToU32(color));

		indices[indexCount++] = firstVertex + 0;
		indices[indexCount++] = firstVertex + 1;
		indices[indexCount++] = firstVertex + 2;
		indices[indexCount++] = firstVertex + 0;
		indices[indexCount++] = firstVertex + 2;
		indices[indexCount++] = firstVertex + 3;
	}

	void DebugDraw::DrawAABB(const AABB& aabb, f32 thickness, Color color)
	{
		Reserve(48, 72);

		// Get the min and max points of the AABB
		Vec3 min = aabb.min;
		Vec3 max = aabb.max;

		DrawLine(Vec3(min.x, min.y, min.z), Vec3(max.x, min.y, min.z), thickness, color);
		DrawLine(Vec3(max.x, min.y, min.z), Vec3(max.x, min.y, max.z), thickness, color);
		DrawLine(Vec3(max.x, min.y, max.z), Vec3(min.x, min.y, max.z), thickness, color);
		DrawLine(Vec3(min.x, min.y, max.z), Vec3(min.x, min.y, min.z), thickness, color);

		DrawLine(Vec3(min.x, max.y, min.z), Vec3(max.x, max.y, min.z), thickness, color);
		DrawLine(Vec3(max.x, max.y, min.z), Vec3(max.x, max.y, max.z), thickness, color);
		DrawLine(Vec3(max.x, max.y, max.z), Vec3(min.x, max.y, max.z), thickness, color);
		DrawLine(Vec3(min.x, max.y, max.z), Vec3(min.x, max.y, min.z), thickness, color);

		DrawLine(Vec3(min.x, min.y, min.z), Vec3(min.x, max.y, min.z), thickness, color);
		DrawLine(Vec3(max.x, min.y, min.z), Vec3(max.x, max.y, min.z), thickness, color);
		DrawLine(Vec3(max.x, min.y, max.z), Vec3(max.x, max.y, max.z), thickness, color);
		DrawLine(Vec3(min.x, min.y, max.z), Vec3(min.x, max.y, max.z), thickness, color);

	}

	void DebugDraw::DrawFrustum(const Frustum& frustum, f32 thickness, Color color)
	{
		Reserve(48, 72);

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
		DrawLine(ntl, ntr, thickness, color);
		DrawLine(ntr, nbr, thickness, color);
		DrawLine(nbr, nbl, thickness, color);
		DrawLine(nbl, ntl, thickness, color);

		// Draw far plane rectangle
		DrawLine(ftl, ftr, thickness, color);
		DrawLine(ftr, fbr, thickness, color);
		DrawLine(fbr, fbl, thickness, color);
		DrawLine(fbl, ftl, thickness, color);

		// Connect near and far planes
		DrawLine(ntl, ftl, thickness, color);
		DrawLine(ntr, ftr, thickness, color);
		DrawLine(nbr, fbr, thickness, color);
		DrawLine(nbl, fbl, thickness, color);
	}

	void DebugDraw::Draw(GPUCommandBuffer* cmd)
	{

		if (indexCount == 0)
		{
			return;
		}

		if (vertexBuffer == nullptr || vertexBuffer->GetDesc().size <= vertexCapacity * sizeof(DebugDrawVertex))
		{
			if (vertexBuffer) vertexBuffer->Destroy();
			vertexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = vertexCapacity * sizeof(DebugDrawVertex),
				.usage = ResourceUsage::VertexBuffer,
				.hostVisible = true,
				.persistentMapped = true
			});
		}

		if (indexBuffer == nullptr || indexBuffer->GetDesc().size <= indexCapacity * sizeof(u32))
		{
			if (indexBuffer) indexBuffer->Destroy();
			indexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = indexCapacity * sizeof(u32),
				.usage = ResourceUsage::IndexBuffer,
				.hostVisible = true,
				.persistentMapped = true
			});
		}

		memcpy(vertexBuffer->GetMappedData(), vertices, vertexCount * sizeof(DebugDrawVertex));
		memcpy(indexBuffer->GetMappedData(), indices, indexCount * sizeof(u32));

		//reset
		cmd->BindVertexBuffer(0, vertexBuffer, 0);
		cmd->BindIndexBuffer(indexBuffer, 0, IndexType::Uint32);
		cmd->DrawIndexed(indexCount, 1, 0, 0, 0);

		vertexCount = 0;
		indexCount = 0;
	}

	DebugDraw::~DebugDraw()
	{
		if (vertexBuffer) vertexBuffer->Destroy();
		if (indexBuffer) indexBuffer->Destroy();
	}

	void DebugDraw::Reserve(u32 reserveVertexCount, u32 reserveIndexCount)
	{
		if (vertexCount + reserveVertexCount >= vertexCapacity)
		{
			vertexCapacity = (vertexCapacity + reserveVertexCount) * 2;
			vertices = ReallocElements<DebugDrawVertex>(vertices, vertexCapacity);
		}

		if (indexCount + reserveIndexCount >= indexCapacity)
		{
			indexCapacity = (indexCapacity + reserveIndexCount) * 2;
			indices = ReallocElements<u32>(indices, indexCapacity);
		}
	}
}