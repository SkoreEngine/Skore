#include "Skore/Navigation/Navigation.hpp"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>

#include "Skore/Profiler.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::Navigation");

	static constexpr i32 MaxPolys = 2048;
	static constexpr i32 MaxCrowdAgents = 128;

	struct NavigationScene::Context
	{
		dtNavMesh*      navMesh = nullptr;
		dtNavMeshQuery* navQuery = nullptr;
		dtCrowd*        crowd = nullptr;
		u8*             navData = nullptr;
		i32             navDataSize = 0;
		u32             navMeshVersion = 0;

		~Context()
		{
			if (crowd)
			{
				dtFreeCrowd(crowd);
			}
			if (navQuery)
			{
				dtFreeNavMeshQuery(navQuery);
			}
			if (navMesh)
			{
				dtFreeNavMesh(navMesh);
			}
			// navData is owned by navMesh after init, do not free separately
		}
	};

	// Helper: collect world-space triangles from all StaticMeshRenderers in the scene
	static void CollectSceneGeometry(Entity* entity, Array<f32>& vertices, Array<i32>& triangles)
	{
		if (!entity) return;

		auto* meshRenderer = entity->GetComponent<StaticMeshRenderer>();
		if (meshRenderer)
		{
			RID meshRid = meshRenderer->GetMesh();
			if (meshRid)
			{
				ResourceObject meshObject = Resources::Read(meshRid);
				if (meshObject)
				{
					ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
					Span<RID> lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);

					if (buffer && lods.Size() > 0)
					{
						ResourceObject lodObject = Resources::Read(lods[0]);

						u64 verticesOffset = lodObject.GetUInt(MeshLodResource::VerticesOffset);
						u64 verticesCount  = lodObject.GetUInt(MeshLodResource::VerticesCount);
						u64 indicesOffset  = lodObject.GetUInt(MeshLodResource::IndicesOffset);
						u64 indicesCount   = lodObject.GetUInt(MeshLodResource::IndicesCount);

						if (verticesCount > 0 && indicesCount > 0)
						{
							// Read vertex data
							Array<MeshStaticVertex> meshVertices;
							meshVertices.Resize(verticesCount);
							buffer.CopyData(meshVertices.Data(), verticesCount * sizeof(MeshStaticVertex), verticesOffset);

							// Read index data
							Array<u32> meshIndices;
							meshIndices.Resize(indicesCount);
							buffer.CopyData(meshIndices.Data(), indicesCount * sizeof(u32), indicesOffset);

							Mat4 worldTransform = entity->GetWorldTransform();
							i32 baseVertex = static_cast<i32>(vertices.Size() / 3);

							// Transform vertices to world space
							for (u64 i = 0; i < verticesCount; i++)
							{
								Vec4 worldPos = worldTransform * Vec4(meshVertices[i].position, 1.0f);
								vertices.EmplaceBack(worldPos.x);
								vertices.EmplaceBack(worldPos.y);
								vertices.EmplaceBack(worldPos.z);
							}

							// Add indices (offset by base vertex)
							for (u64 i = 0; i < indicesCount; i++)
							{
								triangles.EmplaceBack(baseVertex + static_cast<i32>(meshIndices[i]));
							}
						}
					}
				}
			}
		}

		// Recurse into children
		for (Entity* child : entity->GetChildren())
		{
			CollectSceneGeometry(child, vertices, triangles);
		}
	}

	NavigationScene::NavigationScene()
	{
		context = Alloc<Context>();
	}

	NavigationScene::~NavigationScene()
	{
		if (context)
		{
			DestroyAndFree(context);
		}
	}

	void NavigationScene::BuildNavMesh(Scene* scene, const NavMeshBuildSettings& settings)
	{
		if (!context) return;

		// Clean up previous navmesh
		if (context->crowd)
		{
			dtFreeCrowd(context->crowd);
			context->crowd = nullptr;
		}
		if (context->navQuery)
		{
			dtFreeNavMeshQuery(context->navQuery);
			context->navQuery = nullptr;
		}
		if (context->navMesh)
		{
			dtFreeNavMesh(context->navMesh);
			context->navMesh = nullptr;
		}
		context->navData = nullptr;
		context->navDataSize = 0;

		// Collect geometry from scene
		Array<f32> vertices;
		Array<i32> triangles;

		for (Entity* entity : scene->GetEntities())
		{
			CollectSceneGeometry(entity, vertices, triangles);
		}

		i32 nverts = static_cast<i32>(vertices.Size() / 3);
		i32 ntris = static_cast<i32>(triangles.Size() / 3);

		if (nverts == 0 || ntris == 0)
		{
			logger.Warn("No geometry found for navmesh building");
			return;
		}

		// Calculate bounding box
		f32 bmin[3] = { vertices[0], vertices[1], vertices[2] };
		f32 bmax[3] = { vertices[0], vertices[1], vertices[2] };
		for (i32 i = 1; i < nverts; i++)
		{
			f32 x = vertices[i * 3 + 0];
			f32 y = vertices[i * 3 + 1];
			f32 z = vertices[i * 3 + 2];
			bmin[0] = rcMin(bmin[0], x); bmin[1] = rcMin(bmin[1], y); bmin[2] = rcMin(bmin[2], z);
			bmax[0] = rcMax(bmax[0], x); bmax[1] = rcMax(bmax[1], y); bmax[2] = rcMax(bmax[2], z);
		}

		// Initialize Recast config
		rcConfig cfg{};
		cfg.cs = settings.cellSize;
		cfg.ch = settings.cellHeight;
		cfg.walkableSlopeAngle = settings.agentMaxSlope;
		cfg.walkableHeight = static_cast<i32>(ceilf(settings.agentHeight / cfg.ch));
		cfg.walkableClimb = static_cast<i32>(floorf(settings.agentMaxClimb / cfg.ch));
		cfg.walkableRadius = static_cast<i32>(ceilf(settings.agentRadius / cfg.cs));
		cfg.maxEdgeLen = static_cast<i32>(settings.edgeMaxLen / cfg.cs);
		cfg.maxSimplificationError = settings.edgeMaxError;
		cfg.minRegionArea = static_cast<i32>(settings.regionMinSize * settings.regionMinSize);
		cfg.mergeRegionArea = static_cast<i32>(settings.regionMergeSize * settings.regionMergeSize);
		cfg.maxVertsPerPoly = settings.vertsPerPoly;
		cfg.detailSampleDist = settings.detailSampleDist < 0.9f ? 0 : cfg.cs * settings.detailSampleDist;
		cfg.detailSampleMaxError = cfg.ch * settings.detailSampleMaxError;

		rcVcopy(cfg.bmin, bmin);
		rcVcopy(cfg.bmax, bmax);
		rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

		logger.Info("Building navmesh: {}x{} grid, {} vertices, {} triangles", cfg.width, cfg.height, nverts, ntris);

		rcContext ctx;

		// Step 1: Create heightfield
		rcHeightfield* solid = rcAllocHeightfield();
		if (!solid)
		{
			logger.Error("Failed to allocate heightfield");
			return;
		}

		if (!rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
		{
			logger.Error("Failed to create heightfield");
			rcFreeHeightField(solid);
			return;
		}

		// Step 2: Rasterize triangles
		Array<u8> triAreas;
		triAreas.Resize(ntris, 0);

		rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, vertices.Data(), nverts, triangles.Data(), ntris, triAreas.Data());
		if (!rcRasterizeTriangles(&ctx, vertices.Data(), nverts, triangles.Data(), triAreas.Data(), ntris, *solid, cfg.walkableClimb))
		{
			logger.Error("Failed to rasterize triangles");
			rcFreeHeightField(solid);
			return;
		}

		// Step 3: Filter walkable surfaces
		rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
		rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

		// Step 4: Create compact heightfield
		rcCompactHeightfield* chf = rcAllocCompactHeightfield();
		if (!chf)
		{
			logger.Error("Failed to allocate compact heightfield");
			rcFreeHeightField(solid);
			return;
		}

		if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
		{
			logger.Error("Failed to build compact heightfield");
			rcFreeCompactHeightfield(chf);
			rcFreeHeightField(solid);
			return;
		}

		rcFreeHeightField(solid);

		// Step 5: Erode walkable area
		if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
		{
			logger.Error("Failed to erode walkable area");
			rcFreeCompactHeightfield(chf);
			return;
		}

		// Step 6: Build distance field and regions
		if (!rcBuildDistanceField(&ctx, *chf))
		{
			logger.Error("Failed to build distance field");
			rcFreeCompactHeightfield(chf);
			return;
		}

		if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			logger.Error("Failed to build regions");
			rcFreeCompactHeightfield(chf);
			return;
		}

		// Step 7: Create contours
		rcContourSet* cset = rcAllocContourSet();
		if (!cset)
		{
			logger.Error("Failed to allocate contour set");
			rcFreeCompactHeightfield(chf);
			return;
		}

		if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
		{
			logger.Error("Failed to build contours");
			rcFreeContourSet(cset);
			rcFreeCompactHeightfield(chf);
			return;
		}

		// Step 8: Build polygon mesh
		rcPolyMesh* pmesh = rcAllocPolyMesh();
		if (!pmesh)
		{
			logger.Error("Failed to allocate poly mesh");
			rcFreeContourSet(cset);
			rcFreeCompactHeightfield(chf);
			return;
		}

		if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
		{
			logger.Error("Failed to build poly mesh");
			rcFreePolyMesh(pmesh);
			rcFreeContourSet(cset);
			rcFreeCompactHeightfield(chf);
			return;
		}

		// Step 9: Build detail mesh
		rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
		if (!dmesh)
		{
			logger.Error("Failed to allocate detail mesh");
			rcFreePolyMesh(pmesh);
			rcFreeContourSet(cset);
			rcFreeCompactHeightfield(chf);
			return;
		}

		if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
		{
			logger.Error("Failed to build detail mesh");
			rcFreePolyMeshDetail(dmesh);
			rcFreePolyMesh(pmesh);
			rcFreeContourSet(cset);
			rcFreeCompactHeightfield(chf);
			return;
		}

		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);

		// Step 10: Create Detour navmesh data
		if (cfg.maxVertsPerPoly <= DT_VERTS_PER_POLYGON)
		{
			// Set poly flags
			for (i32 i = 0; i < pmesh->npolys; i++)
			{
				pmesh->flags[i] = 1;
			}

			dtNavMeshCreateParams params{};
			params.verts = pmesh->verts;
			params.vertCount = pmesh->nverts;
			params.polys = pmesh->polys;
			params.polyAreas = pmesh->areas;
			params.polyFlags = pmesh->flags;
			params.polyCount = pmesh->npolys;
			params.nvp = pmesh->nvp;
			params.detailMeshes = dmesh->meshes;
			params.detailVerts = dmesh->verts;
			params.detailVertsCount = dmesh->nverts;
			params.detailTris = dmesh->tris;
			params.detailTriCount = dmesh->ntris;
			params.walkableHeight = settings.agentHeight;
			params.walkableRadius = settings.agentRadius;
			params.walkableClimb = settings.agentMaxClimb;
			rcVcopy(params.bmin, pmesh->bmin);
			rcVcopy(params.bmax, pmesh->bmax);
			params.cs = cfg.cs;
			params.ch = cfg.ch;
			params.buildBvTree = true;

			u8* navData = nullptr;
			i32 navDataSize = 0;

			if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
			{
				logger.Error("Failed to create Detour navmesh data");
				rcFreePolyMeshDetail(dmesh);
				rcFreePolyMesh(pmesh);
				return;
			}

			// Initialize dtNavMesh
			context->navMesh = dtAllocNavMesh();
			if (!context->navMesh)
			{
				logger.Error("Failed to allocate dtNavMesh");
				dtFree(navData);
				rcFreePolyMeshDetail(dmesh);
				rcFreePolyMesh(pmesh);
				return;
			}

			dtStatus status = context->navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
			if (dtStatusFailed(status))
			{
				logger.Error("Failed to initialize dtNavMesh");
				dtFree(navData);
				dtFreeNavMesh(context->navMesh);
				context->navMesh = nullptr;
				rcFreePolyMeshDetail(dmesh);
				rcFreePolyMesh(pmesh);
				return;
			}

			context->navData = navData;
			context->navDataSize = navDataSize;

			// Initialize query object
			context->navQuery = dtAllocNavMeshQuery();
			status = context->navQuery->init(context->navMesh, MaxPolys);
			if (dtStatusFailed(status))
			{
				logger.Error("Failed to initialize navmesh query");
				dtFreeNavMeshQuery(context->navQuery);
				context->navQuery = nullptr;
			}

			// Initialize crowd
			context->crowd = dtAllocCrowd();
			if (context->crowd)
			{
				if (!context->crowd->init(MaxCrowdAgents, settings.agentRadius * 2.0f, context->navMesh))
				{
					logger.Error("Failed to initialize crowd");
					dtFreeCrowd(context->crowd);
					context->crowd = nullptr;
				}
			}

			context->navMeshVersion++;
			logger.Info("NavMesh built successfully: {} polys, {} verts", pmesh->npolys, pmesh->nverts);
		}

		rcFreePolyMeshDetail(dmesh);
		rcFreePolyMesh(pmesh);
	}

	void NavigationScene::LoadNavMesh(const u8* data, u32 size)
	{
		if (!context || !data || size == 0) return;

		// Clean up previous
		if (context->crowd)
		{
			dtFreeCrowd(context->crowd);
			context->crowd = nullptr;
		}
		if (context->navQuery)
		{
			dtFreeNavMeshQuery(context->navQuery);
			context->navQuery = nullptr;
		}
		if (context->navMesh)
		{
			dtFreeNavMesh(context->navMesh);
			context->navMesh = nullptr;
		}
		context->navData = nullptr;
		context->navDataSize = 0;

		// Copy data (dtNavMesh takes ownership with DT_TILE_FREE_DATA)
		u8* navData = static_cast<u8*>(dtAlloc(size, DT_ALLOC_PERM));
		memcpy(navData, data, size);

		context->navMesh = dtAllocNavMesh();
		if (!context->navMesh)
		{
			dtFree(navData);
			return;
		}

		dtStatus status = context->navMesh->init(navData, size, DT_TILE_FREE_DATA);
		if (dtStatusFailed(status))
		{
			dtFree(navData);
			dtFreeNavMesh(context->navMesh);
			context->navMesh = nullptr;
			return;
		}

		context->navData = navData;
		context->navDataSize = size;

		// Initialize query
		context->navQuery = dtAllocNavMeshQuery();
		if (context->navQuery)
		{
			status = context->navQuery->init(context->navMesh, MaxPolys);
			if (dtStatusFailed(status))
			{
				dtFreeNavMeshQuery(context->navQuery);
				context->navQuery = nullptr;
			}
		}

		// Initialize crowd
		context->crowd = dtAllocCrowd();
		if (context->crowd)
		{
			if (!context->crowd->init(MaxCrowdAgents, 1.0f, context->navMesh))
			{
				dtFreeCrowd(context->crowd);
				context->crowd = nullptr;
			}
		}

		context->navMeshVersion++;
	}

	u8* NavigationScene::GetNavData() const
	{
		if (!context || !context->navData || context->navDataSize <= 0) return nullptr;
		return context->navData;
	}

	i32 NavigationScene::GetNavDataSize() const
	{
		if (!context || !context->navData || context->navDataSize <= 0) return 0;
		return context->navDataSize;
	}

	bool NavigationScene::FindPath(const Vec3& start, const Vec3& end, NavMeshPath& path)
	{
		if (!context || !context->navQuery || !context->navMesh) return false;

		path.waypoints.Clear();
		path.isPartial = false;

		f32 startPos[3] = { start.x, start.y, start.z };
		f32 endPos[3]   = { end.x, end.y, end.z };
		f32 extents[3]  = { 2.0f, 4.0f, 2.0f };

		dtQueryFilter filter;
		filter.setIncludeFlags(0xFFFF);
		filter.setExcludeFlags(0);

		dtPolyRef startRef = 0;
		dtPolyRef endRef = 0;
		f32 nearestStart[3];
		f32 nearestEnd[3];

		context->navQuery->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart);
		context->navQuery->findNearestPoly(endPos, extents, &filter, &endRef, nearestEnd);

		if (!startRef || !endRef) return false;

		dtPolyRef polyPath[MaxPolys];
		i32 pathCount = 0;

		dtStatus status = context->navQuery->findPath(startRef, endRef, nearestStart, nearestEnd, &filter, polyPath, &pathCount, MaxPolys);
		if (dtStatusFailed(status) || pathCount == 0) return false;

		if (dtStatusDetail(status, DT_PARTIAL_RESULT))
		{
			path.isPartial = true;
		}

		// Find straight path
		f32 straightPath[MaxPolys * 3];
		u8 straightPathFlags[MaxPolys];
		dtPolyRef straightPathPolys[MaxPolys];
		i32 straightPathCount = 0;

		context->navQuery->findStraightPath(nearestStart, nearestEnd, polyPath, pathCount,
			straightPath, straightPathFlags, straightPathPolys, &straightPathCount, MaxPolys);

		for (i32 i = 0; i < straightPathCount; i++)
		{
			path.waypoints.EmplaceBack(Vec3(straightPath[i * 3], straightPath[i * 3 + 1], straightPath[i * 3 + 2]));
		}

		return !path.waypoints.Empty();
	}

	bool NavigationScene::FindNearestPoint(const Vec3& point, Vec3& nearest)
	{
		if (!context || !context->navQuery) return false;

		f32 pos[3] = { point.x, point.y, point.z };
		f32 extents[3] = { 2.0f, 4.0f, 2.0f };

		dtQueryFilter filter;
		filter.setIncludeFlags(0xFFFF);
		filter.setExcludeFlags(0);

		dtPolyRef ref = 0;
		f32 nearestPt[3];

		dtStatus status = context->navQuery->findNearestPoly(pos, extents, &filter, &ref, nearestPt);
		if (dtStatusFailed(status) || !ref) return false;

		nearest = Vec3(nearestPt[0], nearestPt[1], nearestPt[2]);
		return true;
	}

	bool NavigationScene::Raycast(const Vec3& start, const Vec3& end, Vec3& hit, Vec3& normal)
	{
		if (!context || !context->navQuery) return false;

		f32 startPos[3] = { start.x, start.y, start.z };
		f32 endPos[3]   = { end.x, end.y, end.z };
		f32 extents[3]  = { 2.0f, 4.0f, 2.0f };

		dtQueryFilter filter;
		filter.setIncludeFlags(0xFFFF);
		filter.setExcludeFlags(0);

		dtPolyRef startRef = 0;
		f32 nearestStart[3];
		context->navQuery->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart);

		if (!startRef) return false;

		f32 t = 0;
		f32 hitNormal[3] = {};
		dtPolyRef path[MaxPolys];
		i32 pathCount = 0;

		dtStatus status = context->navQuery->raycast(startRef, nearestStart, endPos, &filter, &t, hitNormal, path, &pathCount, MaxPolys);
		if (dtStatusFailed(status)) return false;

		if (t < 1.0f)
		{
			// Hit something
			hit = Vec3(
				nearestStart[0] + (endPos[0] - nearestStart[0]) * t,
				nearestStart[1] + (endPos[1] - nearestStart[1]) * t,
				nearestStart[2] + (endPos[2] - nearestStart[2]) * t
			);
			normal = Vec3(hitNormal[0], hitNormal[1], hitNormal[2]);
			return true;
		}

		return false;
	}

	i32 NavigationScene::AddAgent(const Vec3& position, f32 radius, f32 height, f32 maxSpeed, f32 maxAcceleration)
	{
		if (!context || !context->crowd) return -1;

		dtCrowdAgentParams params{};
		params.radius = radius;
		params.height = height;
		params.maxAcceleration = maxAcceleration;
		params.maxSpeed = maxSpeed;
		params.collisionQueryRange = radius * 12.0f;
		params.pathOptimizationRange = radius * 30.0f;
		params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE;
		params.obstacleAvoidanceType = 3;
		params.separationWeight = 2.0f;

		f32 pos[3] = { position.x, position.y, position.z };
		return context->crowd->addAgent(pos, &params);
	}

	void NavigationScene::RemoveAgent(i32 agentIndex)
	{
		if (!context || !context->crowd || agentIndex < 0) return;
		context->crowd->removeAgent(agentIndex);
	}

	void NavigationScene::SetAgentTarget(i32 agentIndex, const Vec3& target)
	{
		if (!context || !context->crowd || !context->navQuery || agentIndex < 0) return;

		f32 pos[3] = { target.x, target.y, target.z };
		f32 extents[3] = { 2.0f, 4.0f, 2.0f };

		dtQueryFilter filter;
		filter.setIncludeFlags(0xFFFF);
		filter.setExcludeFlags(0);

		dtPolyRef ref = 0;
		f32 nearest[3];
		context->navQuery->findNearestPoly(pos, extents, &filter, &ref, nearest);

		if (ref)
		{
			context->crowd->requestMoveTarget(agentIndex, ref, nearest);
		}
	}

	Vec3 NavigationScene::GetAgentPosition(i32 agentIndex) const
	{
		if (!context || !context->crowd || agentIndex < 0) return Vec3{};

		const dtCrowdAgent* agent = context->crowd->getAgent(agentIndex);
		if (!agent || !agent->active) return Vec3{};

		return Vec3(agent->npos[0], agent->npos[1], agent->npos[2]);
	}

	Vec3 NavigationScene::GetAgentVelocity(i32 agentIndex) const
	{
		if (!context || !context->crowd || agentIndex < 0) return Vec3{};

		const dtCrowdAgent* agent = context->crowd->getAgent(agentIndex);
		if (!agent || !agent->active) return Vec3{};

		return Vec3(agent->vel[0], agent->vel[1], agent->vel[2]);
	}

	void NavigationScene::Update(f32 dt)
	{
		if (!context || !context->crowd) return;
		SK_SCOPED_CPU_ZONE("NavMesh - Update");
		context->crowd->update(dt, nullptr);
	}

	bool NavigationScene::HasNavMesh() const
	{
		return context && context->navMesh != nullptr;
	}

	u32 NavigationScene::GetNavMeshVersion() const
	{
		if (!context) return 0;
		return context->navMeshVersion;
	}

	void NavigationScene::GetNavMeshTriangles(Array<NavMeshDebugVertex>& outVertices) const
	{
		outVertices.Clear();
		if (!context || !context->navMesh) return;

		const dtNavMesh* navMesh = context->navMesh;
		const Vec3 faceColor = Vec3(0.0f, 0.6f, 0.3f);

		for (i32 tileIdx = 0; tileIdx < navMesh->getMaxTiles(); tileIdx++)
		{
			const dtMeshTile* tile = navMesh->getTile(tileIdx);
			if (!tile || !tile->header) continue;

			for (i32 polyIdx = 0; polyIdx < tile->header->polyCount; polyIdx++)
			{
				const dtPoly& poly = tile->polys[polyIdx];
				if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;

				const dtPolyDetail& detail = tile->detailMeshes[polyIdx];

				for (u32 triIdx = 0; triIdx < detail.triCount; triIdx++)
				{
					const u8* tri = &tile->detailTris[(detail.triBase + triIdx) * 4];
					Vec3 triVerts[3];

					for (i32 v = 0; v < 3; v++)
					{
						if (tri[v] < poly.vertCount)
						{
							const f32* vtx = &tile->verts[poly.verts[tri[v]] * 3];
							triVerts[v] = Vec3(vtx[0], vtx[1], vtx[2]);
						}
						else
						{
							const f32* vtx = &tile->detailVerts[(detail.vertBase + tri[v] - poly.vertCount) * 3];
							triVerts[v] = Vec3(vtx[0], vtx[1], vtx[2]);
						}
					}

					outVertices.EmplaceBack(NavMeshDebugVertex{triVerts[0], faceColor});
					outVertices.EmplaceBack(NavMeshDebugVertex{triVerts[1], faceColor});
					outVertices.EmplaceBack(NavMeshDebugVertex{triVerts[2], faceColor});
				}
			}
		}
	}

	// ---- Navigation static class ----

	static NavigationScene* s_currentNavigationScene = nullptr;

	void Navigation::SetCurrentScene(NavigationScene* navigationScene)
	{
		s_currentNavigationScene = navigationScene;
	}

	NavigationScene* Navigation::GetCurrentScene()
	{
		return s_currentNavigationScene;
	}

	bool Navigation::FindPath(const Vec3& start, const Vec3& end, NavMeshPath& path)
	{
		if (!s_currentNavigationScene) return false;
		return s_currentNavigationScene->FindPath(start, end, path);
	}

	bool Navigation::FindNearestPoint(const Vec3& point, Vec3& nearest)
	{
		if (!s_currentNavigationScene) return false;
		return s_currentNavigationScene->FindNearestPoint(point, nearest);
	}

	void Navigation::RegisterType(NativeReflectType<Navigation>& type)
	{
		type.Function<&Navigation::FindPath>("FindPath", "start", "end", "path");
		type.Function<&Navigation::FindNearestPoint>("FindNearestPoint", "point", "nearest");
	}
}