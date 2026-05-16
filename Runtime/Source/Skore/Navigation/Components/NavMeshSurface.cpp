#include "Skore/Navigation/Components/NavMeshSurface.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Navigation/Navigation.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::NavMeshSurface");

	void NavMeshSurface::SetNavMeshData(SubObjectRID<NavMeshResource> navMeshData)
	{
		if (m_navMeshData != navMeshData)
		{
			m_loaded = false;
			m_navMeshData = navMeshData;
			LoadNavMeshData();
		}
	}

	SubObjectRID<NavMeshResource> NavMeshSurface::GetNavMeshData() const
	{
		return m_navMeshData;
	}

	void NavMeshSurface::Create(ComponentSettings& settings)
	{
		LoadNavMeshData();
	}

	void NavMeshSurface::Build()
	{
		if (!scene)
		{
			logger.Error("Cannot build navmesh: no scene");
			return;
		}

		NavMeshBuildSettings settings;
		settings.cellSize             = m_cellSize;
		settings.cellHeight           = m_cellHeight;
		settings.agentHeight          = m_agentHeight;
		settings.agentRadius          = m_agentRadius;
		settings.agentMaxClimb        = m_agentMaxClimb;
		settings.agentMaxSlope        = m_agentMaxSlope;
		settings.regionMinSize        = m_regionMinSize;
		settings.regionMergeSize      = m_regionMergeSize;
		settings.edgeMaxLen           = m_edgeMaxLen;
		settings.edgeMaxError         = m_edgeMaxError;
		settings.vertsPerPoly         = m_vertsPerPoly;
		settings.detailSampleDist     = m_detailSampleDist;
		settings.detailSampleMaxError = m_detailSampleMaxError;

		scene->navigationScene.BuildNavMesh(scene, settings);
	}

	void NavMeshSurface::LoadNavMeshData()
	{
		if (m_loaded) return;

		if (m_navMeshData)
		{
			if (ResourceObject navMeshObj = Resources::Read(m_navMeshData))
			{
				ResourceBuffer buffer = navMeshObj.GetBuffer(NavMeshResource::NavData);
				if (buffer)
				{
					u64 size = buffer.GetSize();
					if (size > 0)
					{
						Array<u8> data;
						data.Resize(size);
						buffer.CopyData(data.Data(), size);
						scene->navigationScene.LoadNavMesh(data.Data(), static_cast<u32>(size));
						m_loaded = true;
					}
				}
			}
		}
	}

	void NavMeshSurface::RegisterType(NativeReflectType<NavMeshSurface>& type)
	{
		type.Field<&NavMeshSurface::m_navMeshData, &NavMeshSurface::GetNavMeshData, &NavMeshSurface::SetNavMeshData>("navMeshData");

		type.Field<&NavMeshSurface::m_cellSize>("cellSize").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.05, .maxValue = 2.0});
		type.Field<&NavMeshSurface::m_cellHeight>("cellHeight").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.05, .maxValue = 2.0});
		type.Field<&NavMeshSurface::m_agentHeight>("agentHeight").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.1, .maxValue = 10.0});
		type.Field<&NavMeshSurface::m_agentRadius>("agentRadius").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.05, .maxValue = 5.0});
		type.Field<&NavMeshSurface::m_agentMaxClimb>("agentMaxClimb").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 5.0});
		type.Field<&NavMeshSurface::m_agentMaxSlope>("agentMaxSlope").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 90.0});
		type.Field<&NavMeshSurface::m_regionMinSize>("regionMinSize").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 150.0});
		type.Field<&NavMeshSurface::m_regionMergeSize>("regionMergeSize").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 150.0});
		type.Field<&NavMeshSurface::m_edgeMaxLen>("edgeMaxLen").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 50.0});
		type.Field<&NavMeshSurface::m_edgeMaxError>("edgeMaxError").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.1, .maxValue = 3.0});
		type.Field<&NavMeshSurface::m_vertsPerPoly>("vertsPerPoly");
		type.Field<&NavMeshSurface::m_detailSampleDist>("detailSampleDist").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 16.0});
		type.Field<&NavMeshSurface::m_detailSampleMaxError>("detailSampleMaxError").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 16.0});

		type.Function<&NavMeshSurface::Build>("Build");

		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false, .category = "Navigation"});
	}
}