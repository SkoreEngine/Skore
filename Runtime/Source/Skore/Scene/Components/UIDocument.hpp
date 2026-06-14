#pragma once

#include "Skore/Scene/Component.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/UI/RmlUI.hpp"

namespace Rml
{
	class Context;
	class ElementDocument;
}

namespace Skore
{
	class SK_API UIDocument : public Component, public Tickable
	{
	public:
		SK_CLASS(UIDocument, Component);

		void Create() override;
		void Destroy() override;

		void OnUpdate(f64 deltaTime) override;

		void SetDocument(RID document);
		RID  GetDocument() const;

		Rml::Context* GetContext() const { return m_context; }

		void UpdateContext(Extent extent);

		static void RegisterType(NativeReflectType<UIDocument>& type);

		//TODO - find a better way of handling it.
		u64 lastFrameRendered = 0;
		u64 lastFrameUpdated = 0;
	private:
		void ReloadDocument();

		TypedRID<UIDocumentResource> m_document = {};
		Rml::Context*                m_context = nullptr;
		Rml::ElementDocument*        m_documentElement = nullptr;
	};
}
