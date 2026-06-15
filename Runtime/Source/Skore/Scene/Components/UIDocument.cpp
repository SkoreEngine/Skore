#include "UIDocument.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void UIDocument::Destroy()
	{
		if (root)
		{
			RmlUI::UnloadDocument(scene->uiContext, root);
		}
		root = {};
	}

	void UIDocument::SetDocument(RID document)
	{
		m_document = document;
		ReloadDocument();
	}

	RID UIDocument::GetDocument() const
	{
		return m_document;
	}

	void UIDocument::ProcessEvent(const EntityEventDesc& event)
	{
		if (!root) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				RmlUI::ShowDocument(root);
				break;
			case EntityEventType::EntityDeactivated:
				RmlUI::HideDocument(root);
				break;
			default:
				break;
		}
	}

	void UIDocument::ReloadDocument()
	{
		if (root)
		{
			RmlUI::UnloadDocument(scene->uiContext, root);
			root = {};
		}

		if (!m_document)
		{
			return;
		}

		if (ResourceObject object = Resources::Read(m_document))
		{
			String content = object.GetString(UIDocumentResource::Content);
			if (!content.Empty())
			{
				root = RmlUI::LoadDocumentFromMemory(scene->uiContext, content);
				if (root)
				{
					RmlUI::ShowDocument(root);
				}
			}
		}
	}

	void UIDocument::RegisterType(NativeReflectType<UIDocument>& type)
	{
		type.Field<&UIDocument::root>("root");
		type.Field<&UIDocument::m_document, &UIDocument::GetDocument, &UIDocument::SetDocument>("document");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
		type.Attribute<Iterable>();
	}


}
