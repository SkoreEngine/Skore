#include "Skore/Scene/SceneCommon.hpp"

#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	RID EntityResource::GetOrCreateComponent(RID entity, TypeID type, UndoRedoScope* scope)
	{
		if (ResourceObject entityObject = Resources::Read(entity))
		{
			for (RID component : entityObject.GetSubObjectList(Components))
			{
				if (Resources::GetType(component)->GetID() == type)
				{
					return component;
				}
			}
		}

		RID newComponent = Resources::Create(type, UUID::RandomUUID(), scope);

		ResourceObject entityObject = Resources::Write(entity);
		entityObject.AddToSubObjectList(Components, newComponent);
		entityObject.Commit(scope);

		return newComponent;
	}

	RID EntityResource::GetOrCreateComponent(ResourceObject& entityObject, TypeID type, UndoRedoScope* scope)
	{
		for (RID component : entityObject.GetSubObjectList(Components))
		{
			if (Resources::GetType(component)->GetID() == type)
			{
				return component;
			}
		}

		RID newComponent = Resources::Create(type, UUID::RandomUUID(), scope);

		entityObject.AddToSubObjectList(Components, newComponent);
		entityObject.Commit(scope);

		return newComponent;
	}
}