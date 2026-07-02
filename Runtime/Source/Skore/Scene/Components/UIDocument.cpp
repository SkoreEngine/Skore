#include "UIDocument.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void UIDocument::OnCreate()
	{
		if (!root)
		{
			ReloadDocument();
		}
	}

	void UIDocument::OnDestroy()
	{
		if (root && m_context)
		{
			m_context->UnloadDocument(root);
		}
		if (m_context)
		{
			m_context->UnregisterDocument(this);
		}
		root = nullptr;
		m_context = nullptr;
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
		switch (event.type)
		{
			case EntityEventType::EntityParentChanged:
			case EntityEventType::UIContextChanged:
				ReloadDocument();
				break;
			case EntityEventType::EntityActivated:
				if (root)
				{
					root->Show();
				}
				break;
			case EntityEventType::EntityDeactivated:
				if (root)
				{
					root->Hide();
				}
				break;
			default:
				break;
		}
	}

	UIContext* UIDocument::FindContext() const
	{
		for (Entity* parent = entity ? entity->GetParent() : nullptr; parent != nullptr; parent = parent->GetParent())
		{
			UIContext* context = parent->GetComponent<UIContext>();
			if (context && context->GetRmlContext())
			{
				return context;
			}
		}

		return nullptr;
	}

	void UIDocument::ReloadDocument()
	{
		if (root && m_context)
		{
			m_context->UnloadDocument(root);
		}
		if (m_context)
		{
			m_context->UnregisterDocument(this);
		}
		root = nullptr;
		m_context = nullptr;

		if (!m_document)
		{
			return;
		}

		UIContext* context = FindContext();
		if (!context)
		{
			return;
		}

		root = context->LoadDocumentFromResource(m_document);
		if (root)
		{
			m_context = context;
			m_context->RegisterDocument(this);
			if (!entity || entity->IsActive())
			{
				root->Show();
			}
		}
	}

	void UIDocument::OnContextDestroyed(UIContext* context)
	{
		if (m_context != context)
		{
			return;
		}

		if (root)
		{
			m_context->UnloadDocument(root);
		}
		m_context->UnregisterDocument(this);
		root = nullptr;
		m_context = nullptr;
	}

	void UIDocument::RegisterType(NativeReflectType<UIDocument>& type)
	{
		type.Field<&UIDocument::root>("root");
		type.Field<&UIDocument::m_document, &UIDocument::GetDocument, &UIDocument::SetDocument>("document");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
		type.Attribute<Iterable>();
	}


}
