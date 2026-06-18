#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Components/UIDocument.hpp"
#include "RmlUI.hpp"

namespace Skore
{
	static void RegisterRmlUIType()
	{
		auto eventPhaseEnum = Reflection::Type<UIEventPhase>();
		eventPhaseEnum.Value<UIEventPhase::None>("None");
		eventPhaseEnum.Value<UIEventPhase::Capture>("Capture");
		eventPhaseEnum.Value<UIEventPhase::Target>("Target");
		eventPhaseEnum.Value<UIEventPhase::Bubble>("Bubble");

		auto modalFlagEnum = Reflection::Type<UIModalFlag>();
		modalFlagEnum.Value<UIModalFlag::None>("None");
		modalFlagEnum.Value<UIModalFlag::Modal>("Modal");
		modalFlagEnum.Value<UIModalFlag::Keep>("Keep");

		auto focusFlagEnum = Reflection::Type<UIFocusFlag>();
		focusFlagEnum.Value<UIFocusFlag::None>("None");
		focusFlagEnum.Value<UIFocusFlag::Document>("Document");
		focusFlagEnum.Value<UIFocusFlag::Keep>("Keep");
		focusFlagEnum.Value<UIFocusFlag::Auto>("Auto");

		auto scrollFlagEnum = Reflection::Type<UIScrollFlag>();
		scrollFlagEnum.Value<UIScrollFlag::None>("None");
		scrollFlagEnum.Value<UIScrollFlag::Auto>("Auto");

		auto uiContext = Reflection::Type<UIContext>();
		uiContext.Function<&UIContext::Create>("Create", "name", "dimensions", "enableResourceSync");
		uiContext.Function<&UIContext::Destroy>("Destroy");
		uiContext.Function<&UIContext::SetDimensions>("SetDimensions", "dimensions");
		uiContext.Function<&UIContext::SetDensityIndependentPixelRatio>("SetDensityIndependentPixelRatio", "ratio");
		uiContext.Function<&UIContext::SetInputTransform>("SetInputTransform", "offset", "scale");
		uiContext.Function<&UIContext::Update>("Update");
		uiContext.Function<&UIContext::Render>("Render");
		uiContext.Function<&UIContext::SetVisible>("SetVisible", "visible");
		uiContext.Function<&UIContext::IsVisible>("IsVisible");
		uiContext.Function<&UIContext::LoadDocumentFromMemory>("LoadDocumentFromMemory", "content");
		uiContext.Function<&UIContext::LoadDocumentFromResource>("LoadDocumentFromResource", "document");
		uiContext.Function<&UIContext::UnloadDocument>("UnloadDocument", "document");
		uiContext.Function<&UIContext::CreateDataModel>("CreateDataModel", "name");
		uiContext.Function<&UIContext::GetDataModel>("GetDataModel", "name");
		uiContext.Function<&UIContext::RemoveDataModel>("RemoveDataModel", "name");

		auto uiElementDocument = Reflection::Type<UIElementDocument>();
		uiElementDocument.Function<&UIElementDocument::Show>("Show", "modalFlag", "focusFlag", "scrollFlag");
		uiElementDocument.Function<&UIElementDocument::Hide>("Hide");
		uiElementDocument.Function<&UIElementDocument::Close>("Close");
		uiElementDocument.Function<&UIElementDocument::PullToFront>("PullToFront");
		uiElementDocument.Function<&UIElementDocument::PushToBack>("PushToBack");
		uiElementDocument.Function<&UIElementDocument::SetTitle>("SetTitle", "title");
		uiElementDocument.Function<&UIElementDocument::GetTitle>("GetTitle");
		uiElementDocument.Function<&UIElementDocument::GetSourceURL>("GetSourceURL");
		uiElementDocument.Function<&UIElementDocument::IsModal>("IsModal");
		uiElementDocument.Function<&UIElementDocument::ReloadStyleSheet>("ReloadStyleSheet");
		uiElementDocument.Function<&UIElementDocument::Update>("Update");
		uiElementDocument.Function<&UIElementDocument::GetContext>("GetContext");
		uiElementDocument.Function<&UIElementDocument::FindNextTabElement>("FindNextTabElement", "currentElement", "forward", "wrapAround");
		uiElementDocument.Function<&UIElementDocument::CreateElement>("CreateElement", "name");
		uiElementDocument.Function<&UIElementDocument::CreateTextNode>("CreateTextNode", "text");

		auto uiElement = Reflection::Type<UIElement>();
		uiElement.Function<&UIElement::Clone>("Clone");
		uiElement.Function<&UIElement::Destroy>("Destroy");
		uiElement.Function<&UIElement::AppendChild>("AppendChild", "child");
		uiElement.Function<&UIElement::InsertBefore>("InsertBefore", "child", "adjacentElement");
		uiElement.Function<&UIElement::RemoveChild>("RemoveChild", "child");
		uiElement.Function<&UIElement::HasChildNodes>("HasChildNodes");
		uiElement.Function<&UIElement::SetClass>("SetClass", "className", "activate");
		uiElement.Function<&UIElement::IsClassSet>("IsClassSet", "className");
		uiElement.Function<&UIElement::SetClassNames>("SetClassNames", "classNames");
		uiElement.Function<&UIElement::GetClassNames>("GetClassNames");
		uiElement.Function<&UIElement::SetPseudoClass>("SetPseudoClass", "pseudoClass", "activate");
		uiElement.Function<&UIElement::IsPseudoClassSet>("IsPseudoClassSet", "pseudoClass");
		uiElement.Function<&UIElement::SetProperty>("SetProperty", "name", "value");
		uiElement.Function<&UIElement::RemoveProperty>("RemoveProperty", "name");
		uiElement.Function<&UIElement::SetAttribute>("SetAttribute", "name", "value");
		uiElement.Function<&UIElement::GetAttribute>("GetAttribute", "name", "defaultValue");
		uiElement.Function<&UIElement::HasAttribute>("HasAttribute", "name");
		uiElement.Function<&UIElement::RemoveAttribute>("RemoveAttribute", "name");
		uiElement.Function<&UIElement::GetTagName>("GetTagName");
		uiElement.Function<&UIElement::GetId>("GetId");
		uiElement.Function<&UIElement::SetId>("SetId", "id");
		uiElement.Function<&UIElement::GetInnerRML>("GetInnerRML");
		uiElement.Function<&UIElement::SetInnerRML>("SetInnerRML", "rml");
		uiElement.Function<&UIElement::IsVisible>("IsVisible", "includeAncestors");
		uiElement.Function<&UIElement::GetAbsoluteLeft>("GetAbsoluteLeft");
		uiElement.Function<&UIElement::GetAbsoluteTop>("GetAbsoluteTop");
		uiElement.Function<&UIElement::GetClientWidth>("GetClientWidth");
		uiElement.Function<&UIElement::GetClientHeight>("GetClientHeight");
		uiElement.Function<&UIElement::GetOffsetWidth>("GetOffsetWidth");
		uiElement.Function<&UIElement::GetOffsetHeight>("GetOffsetHeight");
		uiElement.Function<&UIElement::GetScrollLeft>("GetScrollLeft");
		uiElement.Function<&UIElement::SetScrollLeft>("SetScrollLeft", "scrollLeft");
		uiElement.Function<&UIElement::GetScrollTop>("GetScrollTop");
		uiElement.Function<&UIElement::SetScrollTop>("SetScrollTop", "scrollTop");
		uiElement.Function<&UIElement::GetScrollWidth>("GetScrollWidth");
		uiElement.Function<&UIElement::GetScrollHeight>("GetScrollHeight");
		uiElement.Function<&UIElement::Focus>("Focus", "focusVisible");
		uiElement.Function<&UIElement::Blur>("Blur");
		uiElement.Function<&UIElement::Click>("Click");
		uiElement.Function<&UIElement::ScrollIntoView>("ScrollIntoView", "alignWithTop");
		uiElement.Function<&UIElement::GetParentNode>("GetParentNode");
		uiElement.Function<&UIElement::GetNextSibling>("GetNextSibling");
		uiElement.Function<&UIElement::GetPreviousSibling>("GetPreviousSibling");
		uiElement.Function<&UIElement::GetFirstChild>("GetFirstChild");
		uiElement.Function<&UIElement::GetLastChild>("GetLastChild");
		uiElement.Function<&UIElement::GetChild>("GetChild", "index");
		uiElement.Function<&UIElement::GetNumChildren>("GetNumChildren");
		uiElement.Function<&UIElement::GetOwnerDocument>("GetOwnerDocument");
		uiElement.Function<&UIElement::GetContext>("GetContext");
		uiElement.Function<&UIElement::GetElementById>("GetElementById", "id");
		uiElement.Function<&UIElement::QuerySelector>("QuerySelector", "selector");
		uiElement.Function<&UIElement::QuerySelectorAll>("QuerySelectorAll", "selector");
		uiElement.Function<&UIElement::GetElementsByTagName>("GetElementsByTagName", "tag");
		uiElement.Function<&UIElement::GetElementsByClassName>("GetElementsByClassName", "className");
		uiElement.Function<&UIElement::Closest>("Closest", "selectors");
		uiElement.Function<&UIElement::Matches>("Matches", "selector");
		uiElement.Function<&UIElement::Contains>("Contains", "other");
		uiElement.Function<static_cast<UIEventListener* (UIElement::*)(StringView, FnUIEventCallback, VoidPtr, bool)>(&UIElement::AddEventListener)>("AddEventListener", "event", "callback", "userData", "inCapturePhase");
		uiElement.Function<&UIElement::RemoveEventListener>("RemoveEventListener", "event", "listener", "inCapturePhase");
		uiElement.Function<&UIElement::DispatchEvent>("DispatchEvent", "type");

		auto uiEvent = Reflection::Type<UIEvent>();
		uiEvent.Function<&UIEvent::GetType>("GetType");
		uiEvent.Function<&UIEvent::GetTargetElement>("GetTargetElement");
		uiEvent.Function<&UIEvent::GetCurrentElement>("GetCurrentElement");
		uiEvent.Function<&UIEvent::GetPhase>("GetPhase");
		uiEvent.Function<&UIEvent::IsInterruptible>("IsInterruptible");
		uiEvent.Function<&UIEvent::IsPropagating>("IsPropagating");
		uiEvent.Function<&UIEvent::StopPropagation>("StopPropagation");
		uiEvent.Function<&UIEvent::StopImmediatePropagation>("StopImmediatePropagation");
		uiEvent.Function<&UIEvent::GetUnprojectedMouseScreenPos>("GetUnprojectedMouseScreenPos");
		uiEvent.Function<&UIEvent::GetParameterString>("GetParameterString", "key", "defaultValue");
		uiEvent.Function<&UIEvent::GetParameterFloat>("GetParameterFloat", "key", "defaultValue");
		uiEvent.Function<&UIEvent::GetParameterInt>("GetParameterInt", "key", "defaultValue");
		uiEvent.Function<&UIEvent::GetParameterBool>("GetParameterBool", "key", "defaultValue");

		Reflection::Type<UIEventListener>();

		auto uiDataModel = Reflection::Type<UIDataModel>();
		uiDataModel.Function<&UIDataModel::IsVariableDirty>("IsVariableDirty", "variableName");
		uiDataModel.Function<&UIDataModel::DirtyVariable>("DirtyVariable", "variableName");
		uiDataModel.Function<&UIDataModel::DirtyAllVariables>("DirtyAllVariables");

		auto uiDataVariant = Reflection::Type<UIDataVariant>();
		uiDataVariant.Function<&UIDataVariant::GetString>("GetString", "defaultValue");
		uiDataVariant.Function<&UIDataVariant::SetString>("SetString", "value");
		uiDataVariant.Function<&UIDataVariant::GetFloat>("GetFloat", "defaultValue");
		uiDataVariant.Function<&UIDataVariant::SetFloat>("SetFloat", "value");
		uiDataVariant.Function<&UIDataVariant::GetInt>("GetInt", "defaultValue");
		uiDataVariant.Function<&UIDataVariant::SetInt>("SetInt", "value");
		uiDataVariant.Function<&UIDataVariant::GetBool>("GetBool", "defaultValue");
		uiDataVariant.Function<&UIDataVariant::SetBool>("SetBool", "value");

		auto uiDataModelConstructor = Reflection::Type<UIDataModelConstructor>();
		uiDataModelConstructor.Function<&UIDataModelConstructor::Destroy>("Destroy");
		uiDataModelConstructor.Function<&UIDataModelConstructor::GetModelHandle>("GetModelHandle");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, FnUIDataGetCallback, VoidPtr, FnUIDataSetCallback, VoidPtr)>(&UIDataModelConstructor::BindFunc)>("BindFunc", "name", "getCallback", "getCallbackData", "setCallback", "setCallbackData");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, FnUIDataEventCallback, VoidPtr)>(&UIDataModelConstructor::BindEventCallback)>("BindEventCallback", "name", "callback", "userData");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, f32*)>(&UIDataModelConstructor::BindVariable)>("BindVariable", "name", "ptr");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, i32*)>(&UIDataModelConstructor::BindVariable)>("BindVariable", "name", "ptr");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, bool*)>(&UIDataModelConstructor::BindVariable)>("BindVariable", "name", "ptr");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, f32 (*)(VoidPtr), VoidPtr, void (*)(f32, VoidPtr), VoidPtr)>(&UIDataModelConstructor::BindScalar)>("BindScalar", "name", "get", "getData", "set", "setData");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, i32 (*)(VoidPtr), VoidPtr, void (*)(i32, VoidPtr), VoidPtr)>(&UIDataModelConstructor::BindScalar)>("BindScalar", "name", "get", "getData", "set", "setData");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, bool (*)(VoidPtr), VoidPtr, void (*)(bool, VoidPtr), VoidPtr)>(&UIDataModelConstructor::BindScalar)>("BindScalar", "name", "get", "getData", "set", "setData");
		uiDataModelConstructor.Function<static_cast<bool (UIDataModelConstructor::*)(StringView, String (*)(VoidPtr), VoidPtr, void (*)(StringView, VoidPtr), VoidPtr)>(&UIDataModelConstructor::BindScalar)>("BindScalar", "name", "get", "getData", "set", "setData");
	}

	void RegisterUITypes()
	{
		Reflection::Type<UIDocument>();

		RegisterRmlUIType();

		Resources::Type<UIDocumentResource>()
			.Field<UIDocumentResource::Name>(ResourceFieldType::String)
			.Field<UIDocumentResource::Content>(ResourceFieldType::String)
			.Build();

		Resources::Type<UIStyleResource>()
			.Field<UIStyleResource::Name>(ResourceFieldType::String)
			.Field<UIStyleResource::Content>(ResourceFieldType::String)
			.Build();
	}
}
