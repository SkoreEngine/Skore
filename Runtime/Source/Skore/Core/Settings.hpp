#pragma once
#include "Serialization.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	struct EditableSettings
	{
		String path;
		TypeID type;
		i32    order;
	};

	struct SettingTypeResource
	{
		enum
		{
			Settings		//SubobjectList
		};
	};

	class SK_API Settings
	{
	public:
		static void CreateDefault(ArchiveWriter& writer, TypeID settingsType);
		static RID  Get(TypeID settingsType, TypeID typeId);
		static RID  Load(ArchiveReader&reader, TypeID settingsType);
		static void Save(ArchiveWriter& writer, TypeID settingsType);

		template <typename T1, typename T2>
		static RID Get()
		{
			return Get(TypeInfo<T1>::ID(), TypeInfo<T2>::ID());
		}
	};
}
