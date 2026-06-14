#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Components/UIDocument.hpp"
#include "RmlUI.hpp"

namespace Skore
{
	void RegisterUITypes()
	{
		Reflection::Type<UIDocument>();

		Resources::Type<UIDocumentResource>()
			.Field<UIDocumentResource::Name>(ResourceFieldType::String)
			.Field<UIDocumentResource::Content>(ResourceFieldType::String)
			.Build();

		Resources::Type<UIStyleResource>()
			.Field<UIStyleResource::Name>(ResourceFieldType::String)
			.Field<UIStyleResource::Content>(ResourceFieldType::String)
			.Build();
	}
}
