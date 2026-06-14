#include "UIDocument.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void UIDocument::SetDocument(RID document)
	{
		m_document = document;
	}

	RID UIDocument::GetDocument() const
	{
		return m_document;
	}

	void UIDocument::RegisterType(NativeReflectType<UIDocument>& type)
	{
		type.Field<&UIDocument::m_document, &UIDocument::GetDocument, &UIDocument::SetDocument>("document");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
		type.Attribute<Iterable>();
	}
}
