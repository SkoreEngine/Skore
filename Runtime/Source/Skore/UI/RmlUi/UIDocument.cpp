#include "UIDocument.hpp"

#include <string>

#include "RmlUiManager.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Scene/SceneCommon.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "Skore/App.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	class RenderPipelineContext;

	namespace
	{
		u64 contextCounter = 0;
	}

	void UIDocument::Create()
	{
		Extent      size = Platform::GetWindowSize(Graphics::GetWindow());
		Rml::String name = "UIDocument_" + std::to_string(contextCounter++);

		m_context = Rml::CreateContext(name, Rml::Vector2i(static_cast<int>(size.width), static_cast<int>(size.height)));
		if (!m_context)
		{
			return;
		}

		RmlUiManager::RegisterContext(m_context);

		ReloadDocument();
	}

	void UIDocument::Destroy()
	{
		if (m_context)
		{
			RmlUiManager::UnregisterContext(m_context);

			if (RmlUiManager::GetRenderInterface() != nullptr)
			{
				Rml::RemoveContext(m_context->GetName());
			}
			m_context = nullptr;
			m_documentElement = nullptr;
		}
	}

	void UIDocument::OnUpdate(f64 deltaTime)
	{
		if (RenderPipelineContext* pipelineContext = RenderPipeline::GetMainContext())
		{
			UpdateContext(pipelineContext->GetOutputSize());
		}
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

	void UIDocument::ReloadDocument()
	{
		if (!m_context)
		{
			return;
		}

		if (m_documentElement)
		{
			m_context->UnloadDocument(m_documentElement);
			m_documentElement = nullptr;
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
				m_documentElement = m_context->LoadDocumentFromMemory(Rml::String(content.CStr()));
				if (m_documentElement)
				{
					m_documentElement->Show();
				}
			}
		}
	}

	void UIDocument::UpdateContext(Extent extent)
	{
		if (m_context)
		{
			m_context->SetDimensions(Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
			m_context->SetDensityIndependentPixelRatio(Platform::GetWindowDPI(Graphics::GetWindow()));
			m_context->Update();
			lastFrameUpdated = App::Frame();
		}
	}

	void UIDocument::RegisterType(NativeReflectType<UIDocument>& type)
	{
		type.Field<&UIDocument::m_document, &UIDocument::GetDocument, &UIDocument::SetDocument>("document");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
		type.Attribute<Iterable>();
	}
}
