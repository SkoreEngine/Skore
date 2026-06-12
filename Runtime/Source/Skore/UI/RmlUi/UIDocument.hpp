#pragma once

#include "Skore/Scene/Component.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/UI/RmlUi/RmlUiResources.hpp"

namespace Rml
{
	class Context;
	class ElementDocument;
}

namespace Skore
{
	class SK_API UIDocument : public Component
	{
	public:
		SK_CLASS(UIDocument, Component);

		void Create() override;
		void Destroy() override;

		void SetDocument(RID document);
		RID  GetDocument() const;

		Rml::Context* GetContext() const { return m_context; }

		static void RegisterType(NativeReflectType<UIDocument>& type);

	private:
		void ReloadDocument();

		TypedRID<UIDocumentResource> m_document = {};
		Rml::Context*                m_context = nullptr;
		Rml::ElementDocument*        m_documentElement = nullptr;
	};
}
