#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Traits.hpp"

#include <functional>

namespace Skore
{
	class RmlUIContext;
	class UIElementDocument;
	class UIElement;
	class UIEvent;
	class UIEventListener;
	class UIDataModel;
	class UIDataModelConstructor;
	class UIDataVariant;

	typedef void (*FnUIEventCallback)(UIEvent* event, VoidPtr userData);
	typedef void (*FnUIDataGetCallback)(UIDataVariant* variant, VoidPtr userData);
	typedef void (*FnUIDataSetCallback)(UIDataVariant* variant, VoidPtr userData);
	typedef void (*FnUIDataEventCallback)(UIDataModel* model, UIEvent* event, VoidPtr userData);

	using FnUIEvent     = std::function<void(UIEvent*)>;
	using FnUIDataGet   = std::function<void(UIDataVariant*)>;
	using FnUIDataSet   = std::function<void(UIDataVariant*)>;
	using FnUIDataEvent = std::function<void(UIDataModel*, UIEvent*)>;

	enum class UIEventPhase
	{
		None    = 0,
		Capture = 1,
		Target  = 2,
		Bubble  = 4,
	};

	enum class UIModalFlag
	{
		None,
		Modal,
		Keep,
	};

	enum class UIFocusFlag
	{
		None,
		Document,
		Keep,
		Auto,
	};

	enum class UIScrollFlag
	{
		None,
		Auto,
	};

	struct UIDocumentResource
	{
		enum
		{
			Name,
			Content,
		};
	};

	struct UIStyleResource
	{
		enum
		{
			Name,
			Content,
		};
	};

	class SK_API UIDataVariant
	{
	public:
		String GetString(StringView defaultValue = {});
		void   SetString(StringView value);
		f32    GetFloat(f32 defaultValue = 0.0f);
		void   SetFloat(f32 value);
		i32    GetInt(i32 defaultValue = 0);
		void   SetInt(i32 value);
		bool   GetBool(bool defaultValue = false);
		void   SetBool(bool value);
	};

	class SK_API UIDataModel
	{
	public:
		bool IsVariableDirty(StringView variableName);
		void DirtyVariable(StringView variableName);
		void DirtyAllVariables();
	};

	class SK_API UIEventListener
	{
	};

	class SK_API UIEvent
	{
	public:
		String       GetType();
		UIElement*   GetTargetElement();
		UIElement*   GetCurrentElement();
		UIEventPhase GetPhase();
		bool         IsInterruptible();
		bool         IsPropagating();
		void         StopPropagation();
		void         StopImmediatePropagation();
		Vec2         GetUnprojectedMouseScreenPos();

		String GetParameterString(StringView key, StringView defaultValue = {});
		f32    GetParameterFloat(StringView key, f32 defaultValue = 0.0f);
		i32    GetParameterInt(StringView key, i32 defaultValue = 0);
		bool   GetParameterBool(StringView key, bool defaultValue = false);
	};

	class SK_API UIElement
	{
	public:
		UIElement* Clone();
		void       Destroy();

		UIElement* AppendChild(UIElement* child);
		UIElement* InsertBefore(UIElement* child, UIElement* adjacentElement);
		UIElement* RemoveChild(UIElement* child);
		bool       HasChildNodes();

		void   SetClass(StringView className, bool activate);
		bool   IsClassSet(StringView className);
		void   SetClassNames(StringView classNames);
		String GetClassNames();

		void SetPseudoClass(StringView pseudoClass, bool activate);
		bool IsPseudoClassSet(StringView pseudoClass);

		bool SetProperty(StringView name, StringView value);
		void RemoveProperty(StringView name);

		void   SetAttribute(StringView name, StringView value);
		String GetAttribute(StringView name, StringView defaultValue = {});
		bool   HasAttribute(StringView name);
		void   RemoveAttribute(StringView name);

		String GetTagName();
		String GetId();
		void   SetId(StringView id);
		String GetInnerRML();
		void   SetInnerRML(StringView rml);
		bool   IsVisible(bool includeAncestors = false);

		f32  GetAbsoluteLeft();
		f32  GetAbsoluteTop();
		f32  GetClientWidth();
		f32  GetClientHeight();
		f32  GetOffsetWidth();
		f32  GetOffsetHeight();
		f32  GetScrollLeft();
		void SetScrollLeft(f32 scrollLeft);
		f32  GetScrollTop();
		void SetScrollTop(f32 scrollTop);
		f32  GetScrollWidth();
		f32  GetScrollHeight();

		bool Focus(bool focusVisible = false);
		void Blur();
		void Click();
		void ScrollIntoView(bool alignWithTop = true);

		UIElement*         GetParentNode();
		UIElement*         GetNextSibling();
		UIElement*         GetPreviousSibling();
		UIElement*         GetFirstChild();
		UIElement*         GetLastChild();
		UIElement*         GetChild(i32 index);
		i32                GetNumChildren();
		UIElementDocument* GetOwnerDocument();
		RmlUIContext*      GetContext();

		UIElement*        GetElementById(StringView id);
		UIElement*        QuerySelector(StringView selector);
		Array<UIElement*> QuerySelectorAll(StringView selector);
		Array<UIElement*> GetElementsByTagName(StringView tag);
		Array<UIElement*> GetElementsByClassName(StringView className);
		UIElement*        Closest(StringView selectors);
		bool              Matches(StringView selector);
		bool              Contains(UIElement* other);

		UIEventListener* AddEventListener(StringView event, FnUIEventCallback callback, VoidPtr userData = nullptr, bool inCapturePhase = false);
		UIEventListener* AddEventListener(StringView event, FnUIEvent callback, bool inCapturePhase = false);
		void             RemoveEventListener(StringView event, UIEventListener* listener, bool inCapturePhase = false);
		bool             DispatchEvent(StringView type);
	};

	class SK_API UIElementDocument
	{
	public:
		void       Show(UIModalFlag modalFlag = UIModalFlag::None, UIFocusFlag focusFlag = UIFocusFlag::Auto, UIScrollFlag scrollFlag = UIScrollFlag::Auto);
		void       Hide();
		void       Close();
		void       PullToFront();
		void       PushToBack();
		void       SetTitle(StringView title);
		String     GetTitle();
		String     GetSourceURL();
		bool       IsModal();
		void       ReloadStyleSheet();
		void       Update();
		RmlUIContext* GetContext();
		UIElement* FindNextTabElement(UIElement* currentElement, bool forward, bool wrapAround = true);

		UIElement* CreateElement(StringView name);
		UIElement* CreateTextNode(StringView text);
	};

	class SK_API UIDataModelConstructor
	{
	public:
		void         Destroy();
		UIDataModel* GetModelHandle();

		bool BindFunc(StringView name,
		              FnUIDataGetCallback getCallback, VoidPtr getCallbackData = nullptr,
		              FnUIDataSetCallback setCallback = nullptr, VoidPtr setCallbackData = nullptr);
		bool BindEventCallback(StringView name, FnUIDataEventCallback callback, VoidPtr userData = nullptr);

		bool BindFunc(StringView name, FnUIDataGet getCallback, FnUIDataSet setCallback = {});
		bool BindEventCallback(StringView name, FnUIDataEvent callback);

		bool BindVariable(StringView name, f32* ptr);
		bool BindVariable(StringView name, i32* ptr);
		bool BindVariable(StringView name, bool* ptr);

		bool BindScalar(StringView name, f32 (*get)(VoidPtr), VoidPtr getData = nullptr, void (*set)(f32, VoidPtr) = nullptr, VoidPtr setData = nullptr);
		bool BindScalar(StringView name, i32 (*get)(VoidPtr), VoidPtr getData = nullptr, void (*set)(i32, VoidPtr) = nullptr, VoidPtr setData = nullptr);
		bool BindScalar(StringView name, bool (*get)(VoidPtr), VoidPtr getData = nullptr, void (*set)(bool, VoidPtr) = nullptr, VoidPtr setData = nullptr);
		bool BindScalar(StringView name, String (*get)(VoidPtr), VoidPtr getData = nullptr, void (*set)(StringView, VoidPtr) = nullptr, VoidPtr setData = nullptr);

		template <typename Get>
		bool BindScalar(StringView name, Get get)
		{
			return BindFunc(name, MakeScalarGet<decltype(get())>(std::move(get)));
		}

		template <typename Get, typename Set>
		bool BindScalar(StringView name, Get get, Set set)
		{
			using R = decltype(get());
			return BindFunc(name, MakeScalarGet<R>(std::move(get)), MakeScalarSet<R>(std::move(set)));
		}

	private:
		template <typename R, typename Get>
		static FnUIDataGet MakeScalarGet(Get get)
		{
			return [get = std::move(get)](UIDataVariant* variant) mutable
			{
				if constexpr (Traits::IsSame<R, f32>)       variant->SetFloat(get());
				else if constexpr (Traits::IsSame<R, i32>)  variant->SetInt(get());
				else if constexpr (Traits::IsSame<R, bool>) variant->SetBool(get());
				else                                        variant->SetString(get());
			};
		}

		template <typename R, typename Set>
		static FnUIDataSet MakeScalarSet(Set set)
		{
			return [set = std::move(set)](UIDataVariant* variant) mutable
			{
				if constexpr (Traits::IsSame<R, f32>)       set(variant->GetFloat());
				else if constexpr (Traits::IsSame<R, i32>)  set(variant->GetInt());
				else if constexpr (Traits::IsSame<R, bool>) set(variant->GetBool());
				else                                        set(variant->GetString());
			};
		}
	};

	class SK_API RmlUIContext
	{
	public:
		static RmlUIContext* Create(StringView name, Extent dimensions, bool enableResourceSync = false);

		void Destroy();
		void SetDimensions(Extent dimensions);
		void SetDensityIndependentPixelRatio(f32 ratio);
		void SetInputTransform(Vec2 offset, f32 scale);
		void Update();
		void Render();
		void SetVisible(bool visible);
		bool IsVisible();

		UIElementDocument* LoadDocumentFromMemory(StringView content);
		UIElementDocument* LoadDocumentFromResource(RID document);
		void               UnloadDocument(UIElementDocument* document);

		UIDataModelConstructor* CreateDataModel(StringView name);
		UIDataModelConstructor* GetDataModel(StringView name);
		bool                    RemoveDataModel(StringView name);
	};
}
