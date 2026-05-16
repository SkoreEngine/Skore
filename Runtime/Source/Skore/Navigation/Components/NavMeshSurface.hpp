#pragma once

#include "Skore/Navigation/NavigationCommon.hpp"
#include "Skore/Navigation/NavigationResources.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class SK_API NavMeshSurface : public Component
	{
	public:
		SK_CLASS(NavMeshSurface, Component);

		void SetNavMeshData(SubObjectRID<NavMeshResource> navMeshData);
		SubObjectRID<NavMeshResource> GetNavMeshData() const;

		void Create() override;
		void Build();

		static void RegisterType(NativeReflectType<NavMeshSurface>& type);

	private:
		bool m_loaded = false;
		SubObjectRID<NavMeshResource> m_navMeshData = {};

		f32 m_cellSize             = 0.3f;
		f32 m_cellHeight           = 0.2f;
		f32 m_agentHeight          = 2.0f;
		f32 m_agentRadius          = 0.6f;
		f32 m_agentMaxClimb        = 0.9f;
		f32 m_agentMaxSlope        = 45.0f;
		f32 m_regionMinSize        = 8.0f;
		f32 m_regionMergeSize      = 20.0f;
		f32 m_edgeMaxLen           = 12.0f;
		f32 m_edgeMaxError         = 1.3f;
		i32 m_vertsPerPoly         = 6;
		f32 m_detailSampleDist     = 6.0f;
		f32 m_detailSampleMaxError = 1.0f;


		void LoadNavMeshData();

	};
}
