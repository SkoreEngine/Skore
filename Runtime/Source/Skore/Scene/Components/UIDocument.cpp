#include "UIDocument.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void UIDocument::Destroy()
	{
		if (root && scene->uiContext)
		{
			scene->uiContext->UnloadDocument(root);
		}
		root = nullptr;
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
				root->Show();
				break;
			case EntityEventType::EntityDeactivated:
				root->Hide();
				break;
			default:
				break;
		}
	}

	void UIDocument::ReloadDocument()
	{
		if (root && scene->uiContext)
		{
			scene->uiContext->UnloadDocument(root);
		}
		root = nullptr;

		if (!m_document || !scene->uiContext)
		{
			return;
		}

		root = scene->uiContext->LoadDocumentFromResource(m_document);
		if (root)
		{
			root->Show();
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
