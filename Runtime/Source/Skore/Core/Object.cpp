#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	ReflectType* Object::GetType() const
	{
		return Reflection::FindTypeById(GetTypeId());
	}

	void Object::Serialize(ArchiveWriter& archiveWriter) const
	{
		Serialization::Serialize(Serialization::FindTypeToSerialize(GetTypeId()), archiveWriter, this);
	}

	void Object::Deserialize(ArchiveReader& archiveReader)
	{
		if (ReflectType* type = Serialization::FindTypeToSerialize(GetTypeId()))
		{
			Serialization::Deserialize(type, archiveReader, this);
		}
	}
}