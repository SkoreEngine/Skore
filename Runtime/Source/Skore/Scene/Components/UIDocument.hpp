#pragma once

#include "Skore/Scene/Component.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Scene/Components/UIContext.hpp"
#include "Skore/UI/RmlUI.hpp"

namespace Skore
{


	class SK_API UIDocument : public Component
	{
	public:
		SK_CLASS(UIDocument, Component);


		void OnCreate() override;
		void OnDestroy() override;

		void SetDocument(RID document);
		RID  GetDocument() const;

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<UIDocument>& type);

		UIElementDocument* root = nullptr;

	private:
		UIContext* FindContext() const;
		void ReloadDocument();
		void OnContextDestroyed(UIContext* context);

		TypedRID<UIDocumentResource> m_document = {};
		UIContext*                   m_context = nullptr;

		friend class UIContext;
	};
}
