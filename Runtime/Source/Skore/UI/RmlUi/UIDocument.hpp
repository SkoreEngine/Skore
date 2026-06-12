#pragma once

#include "Skore/Scene/Component.hpp"
#include "Skore/Core/String.hpp"

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

		void          SetDocument(const String& document);
		const String& GetDocument() const;

		Rml::Context* GetContext() const { return m_context; }

		static void RegisterType(NativeReflectType<UIDocument>& type);

	private:
		void ReloadDocument();

		String                m_document;
		Rml::Context*         m_context = nullptr;
		Rml::ElementDocument* m_documentElement = nullptr;
	};
}
