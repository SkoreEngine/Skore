
#include "Skore/Resource/ResourceReflection.hpp"

#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	ReflectType* ResourceReflection::FindTypeToCast(TypeID typeId)
	{
		if (ReflectType* type = Reflection::FindTypeById(typeId); type != nullptr && (!type->GetFields().Empty() || !type->GetValues().Empty()))
		{
			return type;
		}
		return nullptr;
	}
}