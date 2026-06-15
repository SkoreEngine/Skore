#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	SK_HANDLER(UIContext);
	SK_HANDLER(UIElementDocument);
	SK_HANDLER(UIElement);
	SK_HANDLER(UIEvent);
	SK_HANDLER(UIEventListener);
	SK_HANDLER(UIDataModel);
	SK_HANDLER(UIDataModelConstructor);
	SK_HANDLER(UIDataVariant);

	typedef void (*FnUIEventCallback)(UIEvent event, VoidPtr userData);
	typedef void (*FnUIDataGetCallback)(UIDataVariant variant, VoidPtr userData);
	typedef void (*FnUIDataSetCallback)(UIDataVariant variant, VoidPtr userData);
	typedef void (*FnUIDataEventCallback)(UIDataModel model, UIEvent event, VoidPtr userData);

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

	struct SK_API RmlUI
	{
		static UIContext         CreateContext(StringView name, Extent dimensions);
		static void              RemoveContext(UIContext context);
		static void              SetDimensions(UIContext context, Extent dimensions);
		static void              SetDensityIndependentPixelRatio(UIContext context, f32 ratio);
		static void              SetInputTransform(UIContext context, Vec2 offset, f32 scale);
		static void              Update(UIContext context);
		static void              Render(UIContext context);
		static void              SetContextVisible(UIContext context, bool visible);

		static UIElementDocument LoadDocumentFromMemory(UIContext context, StringView content);
		static void              UnloadDocument(UIContext context, UIElementDocument document);

		static void      ShowDocument(UIElementDocument document, UIModalFlag modalFlag = UIModalFlag::None, UIFocusFlag focusFlag = UIFocusFlag::Auto, UIScrollFlag scrollFlag = UIScrollFlag::Auto);
		static void      HideDocument(UIElementDocument document);
		static void      CloseDocument(UIElementDocument document);
		static void      PullDocumentToFront(UIElementDocument document);
		static void      PushDocumentToBack(UIElementDocument document);
		static void      SetDocumentTitle(UIElementDocument document, StringView title);
		static String    GetDocumentTitle(UIElementDocument document);
		static String    GetDocumentSourceURL(UIElementDocument document);
		static bool      IsDocumentModal(UIElementDocument document);
		static void      ReloadDocumentStyleSheet(UIElementDocument document);
		static void      UpdateDocument(UIElementDocument document);
		static UIContext GetDocumentContext(UIElementDocument document);
		static UIElement FindNextTabElement(UIElementDocument document, UIElement currentElement, bool forward, bool wrapAround = true);

		static UIElement CreateElement(UIElementDocument document, StringView name);
		static UIElement CreateTextNode(UIElementDocument document, StringView text);
		static UIElement CloneElement(UIElement element);
		static void      DestroyElement(UIElement element);

		static UIElement AppendChild(UIElement parent, UIElement child);
		static UIElement InsertBefore(UIElement parent, UIElement child, UIElement adjacentElement);
		static UIElement RemoveChild(UIElement parent, UIElement child);
		static bool      HasChildNodes(UIElement element);

		static void   SetElementClass(UIElement element, StringView className, bool activate);
		static bool   IsElementClassSet(UIElement element, StringView className);
		static void   SetElementClassNames(UIElement element, StringView classNames);
		static String GetElementClassNames(UIElement element);

		static void SetElementPseudoClass(UIElement element, StringView pseudoClass, bool activate);
		static bool IsElementPseudoClassSet(UIElement element, StringView pseudoClass);

		static bool SetElementProperty(UIElement element, StringView name, StringView value);
		static void RemoveElementProperty(UIElement element, StringView name);

		static void   SetElementAttribute(UIElement element, StringView name, StringView value);
		static String GetElementAttribute(UIElement element, StringView name, StringView defaultValue = {});
		static bool   HasElementAttribute(UIElement element, StringView name);
		static void   RemoveElementAttribute(UIElement element, StringView name);

		static String GetElementTagName(UIElement element);
		static String GetElementId(UIElement element);
		static void   SetElementId(UIElement element, StringView id);
		static String GetElementInnerRML(UIElement element);
		static void   SetElementInnerRML(UIElement element, StringView rml);
		static bool   IsElementVisible(UIElement element, bool includeAncestors = false);

		static f32  GetElementAbsoluteLeft(UIElement element);
		static f32  GetElementAbsoluteTop(UIElement element);
		static f32  GetElementClientWidth(UIElement element);
		static f32  GetElementClientHeight(UIElement element);
		static f32  GetElementOffsetWidth(UIElement element);
		static f32  GetElementOffsetHeight(UIElement element);
		static f32  GetElementScrollLeft(UIElement element);
		static void SetElementScrollLeft(UIElement element, f32 scrollLeft);
		static f32  GetElementScrollTop(UIElement element);
		static void SetElementScrollTop(UIElement element, f32 scrollTop);
		static f32  GetElementScrollWidth(UIElement element);
		static f32  GetElementScrollHeight(UIElement element);

		static bool FocusElement(UIElement element, bool focusVisible = false);
		static void BlurElement(UIElement element);
		static void ClickElement(UIElement element);
		static void ScrollElementIntoView(UIElement element, bool alignWithTop = true);

		static UIElement         GetElementParentNode(UIElement element);
		static UIElement         GetElementNextSibling(UIElement element);
		static UIElement         GetElementPreviousSibling(UIElement element);
		static UIElement         GetElementFirstChild(UIElement element);
		static UIElement         GetElementLastChild(UIElement element);
		static UIElement         GetElementChild(UIElement element, i32 index);
		static i32               GetElementNumChildren(UIElement element);
		static UIElementDocument GetElementOwnerDocument(UIElement element);
		static UIContext         GetElementContext(UIElement element);

		static UIElement         GetElementById(UIElement element, StringView id);
		static UIElement         QuerySelector(UIElement element, StringView selector);
		static Array<UIElement>  QuerySelectorAll(UIElement element, StringView selector);
		static Array<UIElement>  GetElementsByTagName(UIElement element, StringView tag);
		static Array<UIElement>  GetElementsByClassName(UIElement element, StringView className);
		static UIElement         Closest(UIElement element, StringView selectors);
		static bool              ElementMatches(UIElement element, StringView selector);
		static bool              ElementContains(UIElement element, UIElement other);

		static UIEventListener AddEventListener(UIElement element, StringView event, FnUIEventCallback callback, VoidPtr userData = nullptr, bool inCapturePhase = false);
		static void            RemoveEventListener(UIElement element, StringView event, UIEventListener listener, bool inCapturePhase = false);
		static bool            DispatchEvent(UIElement element, StringView type);

		static String        GetEventType(UIEvent event);
		static UIElement     GetEventTargetElement(UIEvent event);
		static UIElement     GetEventCurrentElement(UIEvent event);
		static UIEventPhase  GetEventPhase(UIEvent event);
		static bool          IsEventInterruptible(UIEvent event);
		static bool          IsEventPropagating(UIEvent event);
		static void          StopEventPropagation(UIEvent event);
		static void          StopEventImmediatePropagation(UIEvent event);
		static Vec2          GetEventUnprojectedMouseScreenPos(UIEvent event);

		static String GetEventParameterString(UIEvent event, StringView key, StringView defaultValue = {});
		static f32    GetEventParameterFloat(UIEvent event, StringView key, f32 defaultValue = 0.0f);
		static i32    GetEventParameterInt(UIEvent event, StringView key, i32 defaultValue = 0);
		static bool   GetEventParameterBool(UIEvent event, StringView key, bool defaultValue = false);

		static UIDataModelConstructor CreateDataModel(UIContext context, StringView name);
		static UIDataModelConstructor GetDataModel(UIContext context, StringView name);
		static bool                   RemoveDataModel(UIContext context, StringView name);
		static void                   DestroyDataModelConstructor(UIDataModelConstructor constructor);

		static UIDataModel GetModelHandle(UIDataModelConstructor constructor);
		static bool        BindFunc(UIDataModelConstructor constructor, StringView name,
		                       FnUIDataGetCallback getCallback, VoidPtr getCallbackData = nullptr,
		                       FnUIDataSetCallback setCallback = nullptr, VoidPtr setCallbackData = nullptr);
		static bool        BindEventCallback(UIDataModelConstructor constructor, StringView name,
		                       FnUIDataEventCallback callback, VoidPtr userData = nullptr);

		static bool BindVariable(UIDataModelConstructor constructor, StringView name, f32* ptr);
		static bool BindVariable(UIDataModelConstructor constructor, StringView name, i32* ptr);
		static bool BindVariable(UIDataModelConstructor constructor, StringView name, bool* ptr);

		static bool BindScalar(UIDataModelConstructor constructor, StringView name,
		                       f32 (*get)(VoidPtr), VoidPtr getData = nullptr,
		                       void (*set)(f32, VoidPtr) = nullptr, VoidPtr setData = nullptr);
		static bool BindScalar(UIDataModelConstructor constructor, StringView name,
		                       i32 (*get)(VoidPtr), VoidPtr getData = nullptr,
		                       void (*set)(i32, VoidPtr) = nullptr, VoidPtr setData = nullptr);
		static bool BindScalar(UIDataModelConstructor constructor, StringView name,
		                       bool (*get)(VoidPtr), VoidPtr getData = nullptr,
		                       void (*set)(bool, VoidPtr) = nullptr, VoidPtr setData = nullptr);
		static bool BindScalar(UIDataModelConstructor constructor, StringView name,
		                       String (*get)(VoidPtr), VoidPtr getData = nullptr,
		                       void (*set)(StringView, VoidPtr) = nullptr, VoidPtr setData = nullptr);

		static bool IsVariableDirty(UIDataModel model, StringView variableName);
		static void DirtyVariable(UIDataModel model, StringView variableName);
		static void DirtyAllVariables(UIDataModel model);

		static String GetVariantString(UIDataVariant variant, StringView defaultValue = {});
		static void   SetVariantString(UIDataVariant variant, StringView value);
		static f32    GetVariantFloat(UIDataVariant variant, f32 defaultValue = 0.0f);
		static void   SetVariantFloat(UIDataVariant variant, f32 value);
		static i32    GetVariantInt(UIDataVariant variant, i32 defaultValue = 0);
		static void   SetVariantInt(UIDataVariant variant, i32 value);
		static bool   GetVariantBool(UIDataVariant variant, bool defaultValue = false);
		static void   SetVariantBool(UIDataVariant variant, bool value);
	};
}
