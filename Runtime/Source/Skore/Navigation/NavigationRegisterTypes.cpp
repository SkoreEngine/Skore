#include "Skore/Navigation/Navigation.hpp"
#include "Skore/Navigation/NavigationCommon.hpp"
#include "Skore/Navigation/NavigationResources.hpp"
#include "Skore/Navigation/Components/NavMeshSurface.hpp"
#include "Skore/Navigation/Components/NavMeshAgent.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	void RegisterNavigationTypes()
	{
		auto navMeshAreaType = Reflection::Type<NavMeshAreaType>();
		navMeshAreaType.Value<NavMeshAreaType::Walkable>("Walkable");
		navMeshAreaType.Value<NavMeshAreaType::NotWalkable>("NotWalkable");
		navMeshAreaType.Value<NavMeshAreaType::Water>("Water");
		navMeshAreaType.Value<NavMeshAreaType::Grass>("Grass");

		auto buildSettings = Reflection::Type<NavMeshBuildSettings>();
		buildSettings.Field<&NavMeshBuildSettings::cellSize>("cellSize");
		buildSettings.Field<&NavMeshBuildSettings::cellHeight>("cellHeight");
		buildSettings.Field<&NavMeshBuildSettings::agentHeight>("agentHeight");
		buildSettings.Field<&NavMeshBuildSettings::agentRadius>("agentRadius");
		buildSettings.Field<&NavMeshBuildSettings::agentMaxClimb>("agentMaxClimb");
		buildSettings.Field<&NavMeshBuildSettings::agentMaxSlope>("agentMaxSlope");
		buildSettings.Field<&NavMeshBuildSettings::regionMinSize>("regionMinSize");
		buildSettings.Field<&NavMeshBuildSettings::regionMergeSize>("regionMergeSize");
		buildSettings.Field<&NavMeshBuildSettings::edgeMaxLen>("edgeMaxLen");
		buildSettings.Field<&NavMeshBuildSettings::edgeMaxError>("edgeMaxError");
		buildSettings.Field<&NavMeshBuildSettings::vertsPerPoly>("vertsPerPoly");
		buildSettings.Field<&NavMeshBuildSettings::detailSampleDist>("detailSampleDist");
		buildSettings.Field<&NavMeshBuildSettings::detailSampleMaxError>("detailSampleMaxError");

		auto navMeshPath = Reflection::Type<NavMeshPath>();
		navMeshPath.Field<&NavMeshPath::waypoints>("waypoints");
		navMeshPath.Field<&NavMeshPath::isPartial>("isPartial");

		Reflection::Type<Navigation>();
		Reflection::Type<NavMeshSurface>();
		Reflection::Type<NavMeshAgent>();

		Resources::Type<NavMeshResource>()
			.Field<NavMeshResource::Name>(ResourceFieldType::String)
			.Field<NavMeshResource::NavData>(ResourceFieldType::Buffer)
			.Build();
	}
}