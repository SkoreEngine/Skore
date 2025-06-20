// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <imgui.h>

#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/IO/InputTypes.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	class Asset;

	class Object;
	struct ImGuiDrawFieldContext;
	class ReflectType;
	class ReflectField;
	class GPUTexture;
	class GPUTextureView;

	struct SK_API ScopedStyleColor final
	{
		SK_NO_COPY_CONSTRUCTOR(ScopedStyleColor);

		template <typename T>
		ScopedStyleColor(ImGuiCol colorId, T colour)
		{
			ImGui::PushStyleColor(colorId, colour);
		}

		~ScopedStyleColor()
		{
			ImGui::PopStyleColor();
		}
	};

	struct SK_API ScopedStyleVar final
	{
		SK_NO_COPY_CONSTRUCTOR(ScopedStyleVar);

		template <typename T>
		ScopedStyleVar(ImGuiStyleVar styleVar, T value)
		{
			ImGui::PushStyleVar(styleVar, value);
		}

		~ScopedStyleVar()
		{
			ImGui::PopStyleVar();
		}
	};

	struct ImGuiContentItemDesc
	{
		usize       id;
		StringView  label;
		GPUTexture* texture = nullptr;
		bool        selected;
		f32         thumbnailScale;
		bool        renameItem = false;
		bool	    showError = false;
	};

	struct ImGuiContentItemState
	{
		bool   renameFinish;
		String newName;
		bool   hovered;
		bool   clicked;
		bool   enter;
		ImVec2 screenStartPos;
		ImVec2 size;
	};

	struct ImGuiInvisibleHeader
	{
		SK_NO_COPY_CONSTRUCTOR(ImGuiInvisibleHeader);

		ImGuiInvisibleHeader()
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0, 0.0f, 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0, 0.0f, 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0, 0.0f, 0.0f, 0.0f));
		}

		virtual ~ImGuiInvisibleHeader()
		{
			ImGui::PopStyleColor(3);
		}
	};

	// Flags for BasicSlider customization (ImGui-style flags)
	enum ImGuiBasicSliderFlags_
	{
		ImGuiBasicSliderFlags_None       = 0,
		ImGuiBasicSliderFlags_NoInput    = 1 << 0,   // Don't show the input text field
		ImGuiBasicSliderFlags_Highlight  = 1 << 1,   // Use highlighted (blue) color
		ImGuiBasicSliderFlags_NoLabel    = 1 << 2,   // Don't show the label
	};


	enum ImGuiInputTextExtraFlags_
	{
		ImGuiInputTextExtraFlags_None = 0,
		ImGuiInputTextExtraFlags_ShowError = 1 << 0,
	};

	typedef u32 ImGuiInputTextExtraFlags;

	typedef void (*FnImGuiDrawFieldCallback)(const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size);

	struct ImGuiDrawObjectInfo
	{
		Object*                  object;
		VoidPtr                  userData;
		FnImGuiDrawFieldCallback callback = nullptr;
	};

	struct ImGuiDrawFieldDrawCheck
	{
		FieldProps     fieldProps = {};
		ReflectField*  reflectField = nullptr;
		ReflectType*   reflectFieldType = nullptr;
		ResourceField* resourceField = nullptr;
	};

	struct ImGuiDrawFieldContext
	{
		u64                      id = 0;
		Object*                  object = nullptr;
		RID						 rid = {};
		ReflectField*            reflectField = nullptr;
		ReflectType*             reflectFieldType = nullptr;
		ResourceField*           resourceField = nullptr;
		VoidPtr                  userData = nullptr;
		FnImGuiDrawFieldCallback callback = nullptr;
		VoidPtr                  customContext = nullptr;
		String					 scopeName = "";
	};

	typedef bool (*FnCanDrawField)(const ImGuiDrawFieldDrawCheck& check);
	typedef void (*FnDrawField)(const ImGuiDrawFieldContext& context, ConstPtr value);
	typedef VoidPtr (*FnCreateCustomContext)(const ImGuiDrawFieldDrawCheck& check);
	typedef void (*FnDestroyCustomContext)(VoidPtr context);
	typedef bool (*FnObjectFieldVisibilityControl)(Object* object);
	typedef bool (*FnResourceFieldVisibilityControl)(const ResourceObject& object);


	struct ImGuiFieldRenderer
	{
		FnCanDrawField         canDrawField;
		FnDrawField            drawField;
		FnCreateCustomContext  createCustomContext;
		FnDestroyCustomContext destroyCustomContext;
	};


	struct ImGuiDrawResourceInfo
	{
		RID rid;
		VoidPtr userData = nullptr;
		FnImGuiDrawFieldCallback callback = nullptr;
		StringView scopeName = "";
	};

	typedef int ImGuiBasicSliderFlags;

	f32      GetScaleFactor();
	ImGuiKey AsImGuiKey(Key key);

	SK_API void ImGuiCreateDockSpace(ImGuiID dockSpaceId);
	SK_API bool ImGuiBegin(u32 id, const char* name, bool* pOpen, ImGuiWindowFlags flags = 0);
	SK_API bool ImGuiBeginFullscreen(uint32_t id, bool* pOpen = nullptr, ImGuiWindowFlags flags = 0);
	SK_API void ImGuiDockBuilderReset(ImGuiID dockSpaceId);
	SK_API void ImGuiDockBuilderDockWindow(ImGuiID windowId, ImGuiID nodeId);
	SK_API bool ImGuiInputText(u32 idx, String& string, ImGuiInputTextFlags flags = 0, ImGuiInputTextExtraFlags extraFlags = ImGuiInputTextExtraFlags_None);
	SK_API void ImGuiInputTextReadOnly(u32 idx, StringView str, ImGuiInputTextFlags flags = 0);
	SK_API bool ImGuiSearchInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags = 0);
	SK_API bool ImGuiPathInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags = 0);
	SK_API void ImGuiBeginTreeNodeStyle();
	SK_API void ImGuiEndTreeNodeStyle();
	SK_API bool ImGuiTreeNode(VoidPtr id, const char* label, ImGuiTreeNodeFlags flags = 0);
	SK_API bool ImGuiTreeLeaf(VoidPtr id, const char* label, ImGuiTreeNodeFlags flags = 0);
	SK_API bool ImGuiBeginPopupMenu(const char* str, ImGuiWindowFlags popupFlags = 0, bool setSize = true);
	SK_API void ImGuiEndPopupMenu(bool closePopup = true);
	SK_API void ImGuiTextWithLabel(StringView label, StringView text);
	SK_API bool ImGuiSelectionButton(const char* label, bool selected, const ImVec2& sizeArg = ImVec2(0, 0));
	SK_API bool ImGuiBorderedButton(const char* label, const ImVec2& size = ImVec2(0, 0));
	SK_API bool ImGuiBasicSlider(const char* label, float* value, float min, float max, ImGuiBasicSliderFlags flags = ImGuiBasicSliderFlags_None, const char* format = "%.0f");
	SK_API bool ImGuiCollapsingHeaderProps(i32 id, const char* label, bool* buttonClicked);
	SK_API void ImGuiDrawObject(const ImGuiDrawObjectInfo& info);
	SK_API void ImGuiRegisterFieldRenderer(ImGuiFieldRenderer fieldRenderer);
	SK_API void ImGuiRegisterFieldVisibilityControl(TypeID typeId, StringView fieldName, FnObjectFieldVisibilityControl fieldVisibilityControl);
	SK_API void ImGuiCommitFieldChanges(const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size);
	SK_API bool ImGuiCurrentTableHovered();
	SK_API void ImGuiCentralizedText(const char* text);

	SK_API void ImGuiTextureItem(GPUTexture* texture, const ImVec2& image_size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
	SK_API void ImGuiDrawTexture(GPUTexture* texture, const Rect& rect, const ImVec4& tintCol = ImVec4(1, 1, 1, 1));
	SK_API void ImGuiDrawTextureView(GPUTextureView* textureView, const Rect& rect, const ImVec4& tintCol = ImVec4(1, 1, 1, 1));

	SK_API Span<ImGuiFieldRenderer> ImGuiGetFieldRenders();

	SK_API bool                  ImGuiBeginContentTable(const char* id, f32 thumbnailScale);
	SK_API ImGuiContentItemState ImGuiContentItem(const ImGuiContentItemDesc& contentItemDesc);
	SK_API void                  ImGuiEndContentTable();

	SK_API void ImGuiRegisterResourceFieldVisibilityControl(TypeID typeId, StringView fieldName, FnResourceFieldVisibilityControl visibilityControl);
	SK_API void ImGuiDrawResource(const ImGuiDrawResourceInfo& drawResourceInfo);
}
