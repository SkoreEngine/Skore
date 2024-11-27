#pragma once

#include "Lib/imgui.h"
#include "Skore/Common.hpp"
#include "Skore/Platform/PlatformTypes.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/IO/InputTypes.hpp"

using namespace Skore;

namespace Skore
{
    class Asset;
    class TypeHandler;
    class FieldHandler;
}

namespace ImGui
{
    struct StyleColor
    {
        StyleColor(const StyleColor&) = delete;
        StyleColor operator=(const StyleColor&) = delete;

        template <typename T>
        StyleColor(ImGuiCol colorId, T colour)
        {
            ImGui::PushStyleColor(colorId, colour);
        }

        virtual ~StyleColor()
        {
            ImGui::PopStyleColor();
        }
    };

    struct StyleVar
    {
        StyleVar(const StyleVar&) = delete;
        StyleVar operator=(const StyleVar&) = delete;

        template <typename T>
        StyleVar(ImGuiStyleVar styleVar, T value)
        {
            ImGui::PushStyleVar(styleVar, value);
        }

        virtual ~StyleVar()
        {
            ImGui::PopStyleVar();
        }
    };

    SK_API void CreateDockSpace(ImGuiID dockSpaceId);
    SK_API bool Begin(u32 id, const char* name, bool* pOpen, ImGuiWindowFlags flags = 0);
    SK_API bool BeginFullscreen(u32 id, bool* pOpen = nullptr, ImGuiWindowFlags flags = 0);
    SK_API void DockBuilderReset(ImGuiID dockSpaceId);
    SK_API void DockBuilderDockWindow(ImGuiID windowId, ImGuiID nodeId);
    SK_API void CenterWindow(ImGuiCond cond);
    SK_API bool InputText(u32 idx, Skore::String& string, ImGuiInputTextFlags flags = 0);
    SK_API bool SearchInputText(ImGuiID idx, Skore::String& string, ImGuiInputTextFlags flags = 0);
    SK_API void BeginTreeNode();
    SK_API void EndTreeNode();
    SK_API bool TreeNode(u32 id, const char* label, ImGuiTreeNodeFlags flags = 0);
    SK_API bool TreeLeaf(u32 id, const char* label, ImGuiTreeNodeFlags flags = 0);
    SK_API void TextureItem(Texture texture, const ImVec2& image_size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1),
                            const ImVec4& border_col = ImVec4(0, 0, 0, 0));
    SK_API void DrawTexture(Texture texture, const Rect& rect, const ImVec4& tintCol = ImVec4(1, 1, 1, 1));
    SK_API bool BeginPopupMenu(const char* str, ImGuiWindowFlags popupFlags = 0, bool setSize = true);
    SK_API void EndPopupMenu(bool closePopup = true);
    SK_API bool SelectionButton(const char* label, bool selected, const ImVec2& sizeArg = ImVec2(0, 0));
    SK_API bool BorderedButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    SK_API bool CollapsingHeaderProps(i32 id, const char* label, bool* buttonClicked);
    SK_API void DummyRect(ImVec2 min, ImVec2 max);
    SK_API ImVec2 GetParentWorkRectMin();
    SK_API ImVec2 GetParentWorkRectMax();

    SK_API ImU32 TextToColor(const char* str);
    SK_API bool CurrentTableHovered();

    SK_API ImGuiKey GetImGuiKey(Key key);

    //frame controlling
    void Init(Skore::Window window, Skore::Swapchain swapchain);
    void BeginFrame(Skore::Window window, Skore::f64 deltaTime);
    void Render(Skore::RenderCommands& renderCommands);
    void ImGuiShutdown();
}
