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

		auto type = Reflection::Type<RmlUI>();

		type.Function<&RmlUI::CreateContext>("CreateContext", "name", "dimensions", "enableResourceSync");
		type.Function<&RmlUI::RemoveContext>("RemoveContext", "context");
		type.Function<&RmlUI::SetDimensions>("SetDimensions", "context", "dimensions");
		type.Function<&RmlUI::SetDensityIndependentPixelRatio>("SetDensityIndependentPixelRatio", "context", "ratio");
		type.Function<&RmlUI::SetInputTransform>("SetInputTransform", "context", "offset", "scale");
		type.Function<&RmlUI::Update>("Update", "context");
		type.Function<&RmlUI::Render>("Render", "context");
		type.Function<&RmlUI::SetContextVisible>("SetContextVisible", "context", "visible");

		type.Function<&RmlUI::LoadDocumentFromMemory>("LoadDocumentFromMemory", "context", "content");
		type.Function<&RmlUI::UnloadDocument>("UnloadDocument", "context", "document");

		type.Function<&RmlUI::ShowDocument>("ShowDocument", "document", "modalFlag", "focusFlag", "scrollFlag");
		type.Function<&RmlUI::HideDocument>("HideDocument", "document");
		type.Function<&RmlUI::CloseDocument>("CloseDocument", "document");
		type.Function<&RmlUI::PullDocumentToFront>("PullDocumentToFront", "document");
		type.Function<&RmlUI::PushDocumentToBack>("PushDocumentToBack", "document");
		type.Function<&RmlUI::SetDocumentTitle>("SetDocumentTitle", "document", "title");
		type.Function<&RmlUI::GetDocumentTitle>("GetDocumentTitle", "document");
		type.Function<&RmlUI::GetDocumentSourceURL>("GetDocumentSourceURL", "document");
		type.Function<&RmlUI::IsDocumentModal>("IsDocumentModal", "document");
		type.Function<&RmlUI::ReloadDocumentStyleSheet>("ReloadDocumentStyleSheet", "document");
		type.Function<&RmlUI::UpdateDocument>("UpdateDocument", "document");
		type.Function<&RmlUI::GetDocumentContext>("GetDocumentContext", "document");
		type.Function<&RmlUI::FindNextTabElement>("FindNextTabElement", "document", "currentElement", "forward", "wrapAround");

		type.Function<&RmlUI::CreateElement>("CreateElement", "document", "name");
		type.Function<&RmlUI::CreateTextNode>("CreateTextNode", "document", "text");
		type.Function<&RmlUI::CloneElement>("CloneElement", "element");
		type.Function<&RmlUI::DestroyElement>("DestroyElement", "element");

		type.Function<&RmlUI::AppendChild>("AppendChild", "parent", "child");
		type.Function<&RmlUI::InsertBefore>("InsertBefore", "parent", "child", "adjacentElement");
		type.Function<&RmlUI::RemoveChild>("RemoveChild", "parent", "child");
		type.Function<&RmlUI::HasChildNodes>("HasChildNodes", "element");

		type.Function<&RmlUI::SetElementClass>("SetElementClass", "element", "className", "activate");
		type.Function<&RmlUI::IsElementClassSet>("IsElementClassSet", "element", "className");
		type.Function<&RmlUI::SetElementClassNames>("SetElementClassNames", "element", "classNames");
		type.Function<&RmlUI::GetElementClassNames>("GetElementClassNames", "element");

		type.Function<&RmlUI::SetElementPseudoClass>("SetElementPseudoClass", "element", "pseudoClass", "activate");
		type.Function<&RmlUI::IsElementPseudoClassSet>("IsElementPseudoClassSet", "element", "pseudoClass");

		type.Function<&RmlUI::SetElementProperty>("SetElementProperty", "element", "name", "value");
		type.Function<&RmlUI::RemoveElementProperty>("RemoveElementProperty", "element", "name");

		type.Function<&RmlUI::SetElementAttribute>("SetElementAttribute", "element", "name", "value");
		type.Function<&RmlUI::GetElementAttribute>("GetElementAttribute", "element", "name", "defaultValue");
		type.Function<&RmlUI::HasElementAttribute>("HasElementAttribute", "element", "name");
		type.Function<&RmlUI::RemoveElementAttribute>("RemoveElementAttribute", "element", "name");

		type.Function<&RmlUI::GetElementTagName>("GetElementTagName", "element");
		type.Function<&RmlUI::GetElementId>("GetElementId", "element");
		type.Function<&RmlUI::SetElementId>("SetElementId", "element", "id");
		type.Function<&RmlUI::GetElementInnerRML>("GetElementInnerRML", "element");
		type.Function<&RmlUI::SetElementInnerRML>("SetElementInnerRML", "element", "rml");
		type.Function<&RmlUI::IsElementVisible>("IsElementVisible", "element", "includeAncestors");

		type.Function<&RmlUI::GetElementAbsoluteLeft>("GetElementAbsoluteLeft", "element");
		type.Function<&RmlUI::GetElementAbsoluteTop>("GetElementAbsoluteTop", "element");
		type.Function<&RmlUI::GetElementClientWidth>("GetElementClientWidth", "element");
		type.Function<&RmlUI::GetElementClientHeight>("GetElementClientHeight", "element");
		type.Function<&RmlUI::GetElementOffsetWidth>("GetElementOffsetWidth", "element");
		type.Function<&RmlUI::GetElementOffsetHeight>("GetElementOffsetHeight", "element");
		type.Function<&RmlUI::GetElementScrollLeft>("GetElementScrollLeft", "element");
		type.Function<&RmlUI::SetElementScrollLeft>("SetElementScrollLeft", "element", "scrollLeft");
		type.Function<&RmlUI::GetElementScrollTop>("GetElementScrollTop", "element");
		type.Function<&RmlUI::SetElementScrollTop>("SetElementScrollTop", "element", "scrollTop");
		type.Function<&RmlUI::GetElementScrollWidth>("GetElementScrollWidth", "element");
		type.Function<&RmlUI::GetElementScrollHeight>("GetElementScrollHeight", "element");

		type.Function<&RmlUI::FocusElement>("FocusElement", "element", "focusVisible");
		type.Function<&RmlUI::BlurElement>("BlurElement", "element");
		type.Function<&RmlUI::ClickElement>("ClickElement", "element");
		type.Function<&RmlUI::ScrollElementIntoView>("ScrollElementIntoView", "element", "alignWithTop");

		type.Function<&RmlUI::GetElementParentNode>("GetElementParentNode", "element");
		type.Function<&RmlUI::GetElementNextSibling>("GetElementNextSibling", "element");
		type.Function<&RmlUI::GetElementPreviousSibling>("GetElementPreviousSibling", "element");
		type.Function<&RmlUI::GetElementFirstChild>("GetElementFirstChild", "element");
		type.Function<&RmlUI::GetElementLastChild>("GetElementLastChild", "element");
		type.Function<&RmlUI::GetElementChild>("GetElementChild", "element", "index");
		type.Function<&RmlUI::GetElementNumChildren>("GetElementNumChildren", "element");
		type.Function<&RmlUI::GetElementOwnerDocument>("GetElementOwnerDocument", "element");
		type.Function<&RmlUI::GetElementContext>("GetElementContext", "element");

		type.Function<&RmlUI::GetElementById>("GetElementById", "element", "id");
		type.Function<&RmlUI::QuerySelector>("QuerySelector", "element", "selector");
		type.Function<&RmlUI::QuerySelectorAll>("QuerySelectorAll", "element", "selector");
		type.Function<&RmlUI::GetElementsByTagName>("GetElementsByTagName", "element", "tag");
		type.Function<&RmlUI::GetElementsByClassName>("GetElementsByClassName", "element", "className");
		type.Function<&RmlUI::Closest>("Closest", "element", "selectors");
		type.Function<&RmlUI::ElementMatches>("ElementMatches", "element", "selector");
		type.Function<&RmlUI::ElementContains>("ElementContains", "element", "other");

		type.Function<static_cast<UIEventListener (*)(UIElement, StringView, FnUIEventCallback, VoidPtr, bool)>(&RmlUI::AddEventListener)>("AddEventListener", "element", "event", "callback", "userData", "inCapturePhase");
		type.Function<&RmlUI::RemoveEventListener>("RemoveEventListener", "element", "event", "listener", "inCapturePhase");
		type.Function<&RmlUI::DispatchEvent>("DispatchEvent", "element", "type");

		type.Function<&RmlUI::GetEventType>("GetEventType", "event");
		type.Function<&RmlUI::GetEventTargetElement>("GetEventTargetElement", "event");
		type.Function<&RmlUI::GetEventCurrentElement>("GetEventCurrentElement", "event");
		type.Function<&RmlUI::GetEventPhase>("GetEventPhase", "event");
		type.Function<&RmlUI::IsEventInterruptible>("IsEventInterruptible", "event");
		type.Function<&RmlUI::IsEventPropagating>("IsEventPropagating", "event");
		type.Function<&RmlUI::StopEventPropagation>("StopEventPropagation", "event");
		type.Function<&RmlUI::StopEventImmediatePropagation>("StopEventImmediatePropagation", "event");
		type.Function<&RmlUI::GetEventUnprojectedMouseScreenPos>("GetEventUnprojectedMouseScreenPos", "event");

		type.Function<&RmlUI::GetEventParameterString>("GetEventParameterString", "event", "key", "defaultValue");
		type.Function<&RmlUI::GetEventParameterFloat>("GetEventParameterFloat", "event", "key", "defaultValue");
		type.Function<&RmlUI::GetEventParameterInt>("GetEventParameterInt", "event", "key", "defaultValue");
		type.Function<&RmlUI::GetEventParameterBool>("GetEventParameterBool", "event", "key", "defaultValue");

		type.Function<&RmlUI::CreateDataModel>("CreateDataModel", "context", "name");
		type.Function<&RmlUI::GetDataModel>("GetDataModel", "context", "name");
		type.Function<&RmlUI::RemoveDataModel>("RemoveDataModel", "context", "name");
		type.Function<&RmlUI::DestroyDataModelConstructor>("DestroyDataModelConstructor", "constructor");

		type.Function<&RmlUI::GetModelHandle>("GetModelHandle", "constructor");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, FnUIDataGetCallback, VoidPtr, FnUIDataSetCallback, VoidPtr)>(&RmlUI::BindFunc)>("BindFunc", "constructor", "name", "getCallback", "getCallbackData", "setCallback", "setCallbackData");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, FnUIDataEventCallback, VoidPtr)>(&RmlUI::BindEventCallback)>("BindEventCallback", "constructor", "name", "callback", "userData");

		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, f32*)>(&RmlUI::BindVariable)>("BindVariable", "constructor", "name", "ptr");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, i32*)>(&RmlUI::BindVariable)>("BindVariable", "constructor", "name", "ptr");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, bool*)>(&RmlUI::BindVariable)>("BindVariable", "constructor", "name", "ptr");

		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, f32 (*)(VoidPtr), VoidPtr, void (*)(f32, VoidPtr), VoidPtr)>(&RmlUI::BindScalar)>("BindScalar", "constructor", "name", "get", "getData", "set", "setData");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, i32 (*)(VoidPtr), VoidPtr, void (*)(i32, VoidPtr), VoidPtr)>(&RmlUI::BindScalar)>("BindScalar", "constructor", "name", "get", "getData", "set", "setData");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, bool (*)(VoidPtr), VoidPtr, void (*)(bool, VoidPtr), VoidPtr)>(&RmlUI::BindScalar)>("BindScalar", "constructor", "name", "get", "getData", "set", "setData");
		type.Function<static_cast<bool (*)(UIDataModelConstructor, StringView, String (*)(VoidPtr), VoidPtr, void (*)(StringView, VoidPtr), VoidPtr)>(&RmlUI::BindScalar)>("BindScalar", "constructor", "name", "get", "getData", "set", "setData");

		type.Function<&RmlUI::IsVariableDirty>("IsVariableDirty", "model", "variableName");
		type.Function<&RmlUI::DirtyVariable>("DirtyVariable", "model", "variableName");
		type.Function<&RmlUI::DirtyAllVariables>("DirtyAllVariables", "model");

		type.Function<&RmlUI::GetVariantString>("GetVariantString", "variant", "defaultValue");
		type.Function<&RmlUI::SetVariantString>("SetVariantString", "variant", "value");
		type.Function<&RmlUI::GetVariantFloat>("GetVariantFloat", "variant", "defaultValue");
		type.Function<&RmlUI::SetVariantFloat>("SetVariantFloat", "variant", "value");
		type.Function<&RmlUI::GetVariantInt>("GetVariantInt", "variant", "defaultValue");
		type.Function<&RmlUI::SetVariantInt>("SetVariantInt", "variant", "value");
		type.Function<&RmlUI::GetVariantBool>("GetVariantBool", "variant", "defaultValue");
		type.Function<&RmlUI::SetVariantBool>("SetVariantBool", "variant", "value");
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
