#pragma once

#include "Skore/Core/Array.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/UI/RmlUI.hpp"

namespace Skore
{
	class UIDocument;

	class SK_API UIContext : public Component
	{
	public:
		SK_CLASS(UIContext, Component);

		void OnCreate() override;
		void OnStart() override;
		void OnDestroy() override;
		void ProcessEvent(const EntityEventDesc& event) override;

		void SetDimensions(Extent dimensions);
		void SetDensityIndependentPixelRatio(f32 ratio);
		void SetInputTransform(Vec2 offset, f32 scale);
		void Update();
		void Render();

		void SetVisible(bool visible);
		bool GetVisible() const;
		bool IsVisible() const;

		UIElementDocument* LoadDocumentFromMemory(StringView content);
		UIElementDocument* LoadDocumentFromResource(RID document);
		void               UnloadDocument(UIElementDocument* document);

		UIDataModelConstructor* CreateDataModel(StringView name);
		UIDataModelConstructor* GetDataModel(StringView name);
		bool                    RemoveDataModel(StringView name);

		RmlUIContext* GetRmlContext() const;

		static void RegisterType(NativeReflectType<UIContext>& type);

	private:
		void RegisterDocument(UIDocument* document);
		void UnregisterDocument(UIDocument* document);

		void EnsureContext();
		void DestroyContext();
		void ApplyVisibility();
		void NotifyContextChanged();

		RmlUIContext*      m_context = nullptr;
		Array<UIDocument*> m_documents;
		bool               m_visible = true;
		bool               m_destroying = false;

		friend class UIDocument;
	};
}
