#pragma once

#include "NavigationCommon.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class Scene;

	class SK_API NavigationScene
	{
	public:
		struct Context;
		SK_NO_COPY_CONSTRUCTOR(NavigationScene);

		NavigationScene();
		~NavigationScene();

		// NavMesh building
		void BuildNavMesh(Scene* scene, const NavMeshBuildSettings& settings);

		// Serialization
		void LoadNavMesh(const u8* data, u32 size);
		u8* GetNavData() const;
		i32 GetNavDataSize() const;

		// Queries
		bool FindPath(const Vec3& start, const Vec3& end, NavMeshPath& path);
		bool FindNearestPoint(const Vec3& point, Vec3& nearest);
		bool Raycast(const Vec3& start, const Vec3& end, Vec3& hit, Vec3& normal);

		// Crowd simulation
		i32  AddAgent(const Vec3& position, f32 radius, f32 height, f32 maxSpeed, f32 maxAcceleration);
		void RemoveAgent(i32 agentIndex);
		void SetAgentTarget(i32 agentIndex, const Vec3& target);
		Vec3 GetAgentPosition(i32 agentIndex) const;
		Vec3 GetAgentVelocity(i32 agentIndex) const;

		// Tick
		void Update(f32 dt);

		bool HasNavMesh() const;

		// Debug visualization
		u32  GetNavMeshVersion() const;
		void GetNavMeshTriangles(Array<NavMeshDebugVertex>& outVertices) const;

		friend class Scene;
		friend class Navigation;
	private:
		Context* context = nullptr;
	};

	class SK_API Navigation
	{
	public:
		static void SetCurrentScene(NavigationScene* navigationScene);
		static NavigationScene* GetCurrentScene();

		static bool FindPath(const Vec3& start, const Vec3& end, NavMeshPath& path);
		static bool FindNearestPoint(const Vec3& point, Vec3& nearest);

		static void RegisterType(NativeReflectType<Navigation>& type);
	};
}
