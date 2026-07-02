#include "Skore/Scene/Components/UIContext.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Components/UIDocument.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void UIContext::OnCreate()
	{
		EnsureContext();
	}

	void UIContext::OnStart()
	{
		ApplyVisibility();
		NotifyContextChanged();
	}

	void UIContext::OnDestroy()
	{
		m_destroying = true;
		while (!m_documents.Empty())
		{
			UIDocument* document = m_documents.Back();
			document->OnContextDestroyed(this);
			if (!m_documents.Empty() && m_documents.Back() == document)
			{
				m_documents.PopBack();
			}
		}
		DestroyContext();
	}

	void UIContext::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::EntityActivated || event.type == EntityEventType::EntityDeactivated)
		{
			ApplyVisibility();
		}
	}

	void UIContext::SetDimensions(Extent dimensions)
	{
		EnsureContext();
		if (m_context)
		{
			m_context->SetDimensions(dimensions);
		}
	}

	void UIContext::SetDensityIndependentPixelRatio(f32 ratio)
	{
		EnsureContext();
		if (m_context)
		{
			m_context->SetDensityIndependentPixelRatio(ratio);
		}
	}

	void UIContext::SetInputTransform(Vec2 offset, f32 scale)
	{
		EnsureContext();
		if (m_context)
		{
			m_context->SetInputTransform(offset, scale);
		}
	}

	void UIContext::Update()
	{
		EnsureContext();
		if (m_context)
		{
			m_context->Update();
		}
	}

	void UIContext::Render()
	{
		EnsureContext();
		if (m_context)
		{
			m_context->Render();
		}
	}

	void UIContext::SetVisible(bool visible)
	{
		m_visible = visible;
		ApplyVisibility();
	}

	bool UIContext::GetVisible() const
	{
		return m_visible;
	}

	bool UIContext::IsVisible() const
	{
		return m_context != nullptr && m_context->IsVisible();
	}

	UIElementDocument* UIContext::LoadDocumentFromMemory(StringView content)
	{
		EnsureContext();
		return m_context ? m_context->LoadDocumentFromMemory(content) : nullptr;
	}

	UIElementDocument* UIContext::LoadDocumentFromResource(RID document)
	{
		EnsureContext();
		return m_context ? m_context->LoadDocumentFromResource(document) : nullptr;
	}

	void UIContext::UnloadDocument(UIElementDocument* document)
	{
		if (m_context && document)
		{
			m_context->UnloadDocument(document);
		}
	}

	UIDataModelConstructor* UIContext::CreateDataModel(StringView name)
	{
		EnsureContext();
		return m_context ? m_context->CreateDataModel(name) : nullptr;
	}

	UIDataModelConstructor* UIContext::GetDataModel(StringView name)
	{
		EnsureContext();
		return m_context ? m_context->GetDataModel(name) : nullptr;
	}

	bool UIContext::RemoveDataModel(StringView name)
	{
		EnsureContext();
		return m_context && m_context->RemoveDataModel(name);
	}

	RmlUIContext* UIContext::GetRmlContext() const
	{
		return m_destroying ? nullptr : m_context;
	}

	void UIContext::RegisterDocument(UIDocument* document)
	{
		if (document && m_documents.IndexOf(document) == nPos)
		{
			m_documents.EmplaceBack(document);
		}
	}

	void UIContext::UnregisterDocument(UIDocument* document)
	{
		if (document)
		{
			m_documents.Remove(document);
		}
	}

	void UIContext::EnsureContext()
	{
		if (m_context || m_destroying)
		{
			return;
		}

		String contextName = String("UIContext_") + ToString(PtrToInt(this));
		m_context = RmlUIContext::Create(StringView(contextName), Extent(800, 600), scene && scene->IsResourceSyncEnabled());
		ApplyVisibility();
	}

	void UIContext::DestroyContext()
	{
		if (m_context)
		{
			m_context->Destroy();
			m_context = nullptr;
		}
	}

	void UIContext::ApplyVisibility()
	{
		if (m_context)
		{
			const bool active = entity == nullptr || entity->IsActive();
			m_context->SetVisible(m_visible && active && !m_destroying);
		}
	}

	void UIContext::NotifyContextChanged()
	{
		if (!entity)
		{
			return;
		}

		EntityEventDesc event;
		event.type = EntityEventType::UIContextChanged;
		event.eventData = this;

		for (Entity* child : entity->GetChildren())
		{
			child->NotifyEvent(event, true);
		}
	}

	void UIContext::RegisterType(NativeReflectType<UIContext>& type)
	{
		type.Field<&UIContext::m_visible, &UIContext::GetVisible, &UIContext::SetVisible>("visible");

		type.Function<&UIContext::SetDimensions>("SetDimensions", "dimensions");
		type.Function<&UIContext::SetDensityIndependentPixelRatio>("SetDensityIndependentPixelRatio", "ratio");
		type.Function<&UIContext::SetInputTransform>("SetInputTransform", "offset", "scale");
		type.Function<&UIContext::Update>("Update");
		type.Function<&UIContext::Render>("Render");
		type.Function<&UIContext::SetVisible>("SetVisible", "visible");
		type.Function<&UIContext::GetVisible>("GetVisible");
		type.Function<&UIContext::IsVisible>("IsVisible");
		type.Function<&UIContext::LoadDocumentFromMemory>("LoadDocumentFromMemory", "content");
		type.Function<&UIContext::LoadDocumentFromResource>("LoadDocumentFromResource", "document");
		type.Function<&UIContext::UnloadDocument>("UnloadDocument", "document");
		type.Function<&UIContext::CreateDataModel>("CreateDataModel", "name");
		type.Function<&UIContext::GetDataModel>("GetDataModel", "name");
		type.Function<&UIContext::RemoveDataModel>("RemoveDataModel", "name");
		type.Function<&UIContext::GetRmlContext>("GetRmlContext");

		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false, .category = "UI"});
		type.Attribute<Iterable>();
	}
}
