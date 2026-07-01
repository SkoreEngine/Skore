#pragma once

#include <imgui.h>

#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/IO/InputTypes.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	class GPUSampler;
	class Asset;

	class Object;
	struct EditorWindow;
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

	inline ImVec2 ToImVec2(const Vec2& vec)
	{
		return ImVec2(vec.x, vec.y);
	}

	inline Vec2 FromImVec2(const ImVec2& vec)
	{
		return Vec2(vec.x, vec.y);
	}

	struct ImGuiContentItemDesc
	{
		u64         id;
		StringView  label;
		GPUTexture* texture = nullptr;
		const char* icon = "";
		bool        selected;
		f32         thumbnailScale;
		bool        renameItem = false;
		bool        showError = false;
	};

	struct ImGuiContentItemState
	{
		bool   renameFinish;
		String newName;
		bool   hovered;
		bool   clicked;
		bool   released;
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

	struct ImGuiDrawObjectInfo
	{
		Object* object;
		VoidPtr userData;
	};

	struct SK_API ImGuiDrawFieldDrawCheck
	{
		FieldProps        fieldProps = {};
		ReflectField*     reflectField = nullptr;
		ReflectType*      reflectFieldType = nullptr;
		ResourceField*		resourceField = nullptr;

		bool HasAttribute(TypeID typeId) const;

		template <typename T>
		bool HasAttribute() const
		{
			return HasAttribute(sktypeid(T));
		}

		bool IsResourceFieldType(ResourceFieldType resourceFieldType) const;
	};

	struct ImGuiDrawFieldContext
	{
		u64            id = 0;
		Object*        object = nullptr;
		RID            rid = {};
		FieldProps     fieldProps = {};
		ReflectField*  reflectField = nullptr;
		ReflectType*   reflectFieldType = nullptr;
		ResourceField* resourceField = nullptr;
		VoidPtr        userData = nullptr;
		VoidPtr        customContext = nullptr;
		bool           overridden = false;
		StringView     label = "";
		String         scopeName = "";
	};

	typedef bool (*FnCanDrawField)(const ImGuiDrawFieldDrawCheck& check);
	typedef void (*FnDrawField)(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished);
	typedef VoidPtr (*FnCreateCustomContext)(const ImGuiDrawFieldDrawCheck& check);
	typedef void (*FnDestroyCustomContext)(VoidPtr context);
	typedef bool (*FnObjectFieldVisibilityControl)(Object* object);
	typedef bool (*FnResourceFieldVisibilityControl)(const ResourceObject& object);
	typedef void (*FnResourceAction)(const ResourceObject& object);
	typedef void (*FnOnResourceDraw)(const ResourceObject& object);


	struct ImGuiFieldRenderer
	{
		FnCanDrawField         canDrawField;
		FnDrawField            drawField;
		FnCreateCustomContext  createCustomContext;
		FnDestroyCustomContext destroyCustomContext;
		bool									 hideLabel = false;
	};


	struct ImGuiDrawResourceInfo
	{
		RID rid;
		VoidPtr userData = nullptr;
		StringView scopeName = "";

		//workaround to hide asset name.
		bool ignoreFirstItem = false;
		bool drawCollapseHeader = false;
	};

	typedef void (*FnResourceDrawOverride)(const ImGuiDrawResourceInfo& info, const ResourceObject& object);

	typedef int ImGuiBasicSliderFlags;

	f32      GetScaleFactor();
	ImGuiKey AsImGuiKey(Key key);

	SK_API ImTextureID GetImGuiTextureId(GPUTextureView* textureView, GPUSampler* sampler = nullptr);

	SK_API void ImGuiCreateDockSpace(ImGuiID dockSpaceId);
	SK_API bool ImGuiBegin(EditorWindow* window, bool* pOpen, ImGuiWindowFlags flags = 0);
	SK_API bool ImGuiBeginFullscreen(uint32_t id, bool* pOpen = nullptr, ImGuiWindowFlags flags = 0);
	SK_API void ImGuiCenterWindow(ImGuiCond cond);
	SK_API u32  ImGuiHoveredWindowId();
	SK_API void ImGuiDockBuilderReset(ImGuiID dockSpaceId);
	SK_API void ImGuiDockBuilderDockWindow(ImGuiID windowId, ImGuiID nodeId);
	SK_API bool ImGuiInputText(u32 idx, String& string, ImGuiInputTextFlags flags = 0, ImGuiInputTextExtraFlags extraFlags = ImGuiInputTextExtraFlags_None);
	SK_API bool ImGuiInputTextMultiline(u32 idx, String& string, const ImVec2& size, ImGuiInputTextFlags flags = 0, ImGuiInputTextExtraFlags extraFlags = ImGuiInputTextExtraFlags_None);
	SK_API void ImGuiInputTextReadOnly(u32 idx, StringView str, ImGuiInputTextFlags flags = 0);
	SK_API bool ImGuiSearchInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags = 0);
	SK_API bool ImGuiPathInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags = 0);
	SK_API void ImGuiBeginTreeNodeStyle();
	SK_API void ImGuiEndTreeNodeStyle();
	SK_API bool ImGuiTreeNode(ConstPtr id, const char* label, ImGuiTreeNodeFlags flags = 0);
	SK_API bool ImGuiTreeLeaf(ConstPtr id, const char* label, ImGuiTreeNodeFlags flags = 0);
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
	SK_API void ImGuiDrawTextureView(GPUTextureView* textureView, const Rect& rect, const ImVec4& tintCol = ImVec4(1, 1, 1, 1), GPUSampler* sampler = nullptr);

	SK_API Span<ImGuiFieldRenderer> ImGuiGetFieldRenders();

	SK_API bool                  ImGuiBeginContentTable(const char* id, f32 thumbnailScale);
	SK_API ImGuiContentItemState ImGuiContentItem(const ImGuiContentItemDesc& contentItemDesc);
	SK_API void                  ImGuiEndContentTable();

	typedef void (*FnResourceSelectionCallback)(RID rid, VoidPtr userData);
	SK_API void ImGuiResourceSelectionPopup(u64 id, TypeID typeId, RID contextRid, bool open, FnResourceSelectionCallback callback, VoidPtr userData = nullptr);

	SK_API void ImGuiRegisterResourceFieldVisibilityControl(TypeID typeId, StringView fieldName, FnResourceFieldVisibilityControl visibilityControl);
	SK_API void ImGuiRegisterResourceTypeAction(TypeID typeId, StringView actionName, FnResourceAction action);
	SK_API void ImGuiRegisterResourceDrawCallback(TypeID typeId, FnOnResourceDraw drawCallback);
	SK_API void ImGuiRegisterResourceDrawOverride(TypeID typeId, FnResourceDrawOverride drawOverride);
	SK_API void ImGuiDrawResource(const ImGuiDrawResourceInfo& drawResourceInfo);
}
