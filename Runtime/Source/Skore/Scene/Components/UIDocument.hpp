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

		void SetDocument(RID document);
		RID  GetDocument() const;

		static void RegisterType(NativeReflectType<UIDocument>& type);

	private:
		TypedRID<UIDocumentResource> m_document = {};
	};
}
