#pragma once

#include "Skore/Scene/Component.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/UI/RmlUI.hpp"

namespace Skore
{


	class SK_API UIDocument : public Component
	{
	public:
		SK_CLASS(UIDocument, Component);


		void OnDestroy() override;

		void SetDocument(RID document);
		RID  GetDocument() const;

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<UIDocument>& type);

		UIElementDocument* root = nullptr;

	private:
		void ReloadDocument();

		TypedRID<UIDocumentResource> m_document = {};
	};
}
