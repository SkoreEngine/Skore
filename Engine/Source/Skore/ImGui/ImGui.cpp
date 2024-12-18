#include "ImGui.hpp"

#include <functional>

#include "Skore/Platform/Platform.hpp"
#include "Skore/IO/InputTypes.hpp"
#include "ImGuiPlatform.hpp"
#include "Skore/Graphics/Device/RenderDevice.hpp"
#include "IconsFontAwesome6.h"
#include "Skore/Engine.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Core/UniquePtr.hpp"
#include "Skore/ImGui/Lib/imgui_internal.h"
#include "Skore/ImGui/Lib/ImGuizmo.h"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Core/StaticContent.hpp"

using namespace Skore;

namespace Skore
{
    RenderDevice& GetRenderDevice();
}

namespace ImGui
{
    namespace
    {
        f32      scaleFactor = 1.0;
        ImGuiKey keys[static_cast<u32>(Key::MAX)];
    }

    struct InputTextCallback_UserData
    {
        String& str;
    };

    static int InputTextCallback(ImGuiInputTextCallbackData* data)
    {
        auto userData = (InputTextCallback_UserData*)data->UserData;
        if (data->BufTextLen >= (userData->str.Capacity() - 1))
        {
            auto newSize = userData->str.Capacity() + (userData->str.Capacity() * 2) / 3;
            userData->str.Reserve(newSize);
            data->Buf = userData->str.begin();
        }
        userData->str.SetSize(data->BufTextLen);
        return 0;
    }

    void CreateDockSpace(ImGuiID dockSpaceId)
    {
        static ImGuiDockNodeFlags dockNodeFlags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        auto             viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 20, 23, 255));
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        dockNodeFlags |= ImGuiDockNodeFlags_NoWindowMenuButton;

        ImGui::Begin("DockSpace", 0, windowFlags);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(1);

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, viewport->WorkSize.y - 40 * style.ScaleFactor), dockNodeFlags);

        //		auto* drawList = ImGui::GetWindowDrawList();
        //		drawList->AddLine(ImVec2(0.0f, viewport->WorkSize.y - 30), ImVec2(viewport->WorkSize.x, viewport->WorkSize.y), IM_COL32(0, 0, 0, 255), 1.f * style.ScaleFactor);
    }

    bool Begin(u32 id, const char* name, bool* pOpen, ImGuiWindowFlags flags)
    {
        ImGuiStyle& style = ImGui::GetStyle();

        ImGui::SetNextWindowSize(ImVec2(1024, 576) * style.ScaleFactor, ImGuiCond_Once);

        char str[100];
        sprintf(str, "%s###%d", name, id);
        bool open = ImGui::Begin(str, pOpen, flags);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        {
            //hoveredWindowId = id;
        }
        return open;
    }

    bool BeginFullscreen(u32 id, bool* pOpen, ImGuiWindowFlags flags)
    {
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | flags;
        auto viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        char str[20];
        sprintf(str, "###%d", id);
        bool open = ImGui::Begin(str, pOpen, windowFlags);

        ImGui::PopStyleVar(); //ImGuiStyleVar_WindowRounding
        ImGui::PopStyleVar(); //ImGuiStyleVar_WindowBorderSize

        return open;
    }

    void DockBuilderReset(ImGuiID dockSpaceId)
    {
        auto viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);
    }

    void DockBuilderDockWindow(ImGuiID windowId, ImGuiID nodeId)
    {
        char str[20];
        sprintf(str, "###%d", windowId);
        ImGui::DockBuilderDockWindow(str, nodeId);
    }

    void CenterWindow(ImGuiCond cond)
    {
        ImGuiIO& io = ImGui::GetIO();
        SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), cond, ImVec2(0.5f, 0.5f));
    }

    bool InputText(u32 idx, Skore::String& string, ImGuiInputTextFlags flags)
    {
        char str[100];
        sprintf(str, "###txtid%d", idx);

        InputTextCallback_UserData user_data{string};
        flags |= ImGuiInputTextFlags_CallbackResize;

        auto ret = ImGui::InputText(str, string.begin(), string.Capacity() + 1, flags, InputTextCallback, &user_data);

        auto& imguiContext = *ImGui::GetCurrentContext();

        auto& rect = imguiContext.LastItemData.Rect;
        auto* drawList = ImGui::GetWindowDrawList();

        if (ImGui::IsItemFocused())
        {
            drawList->AddRect(rect.Min, ImVec2(rect.Max.x - ImGui::GetStyle().ScaleFactor, rect.Max.y), IM_COL32(66, 140, 199, 255), ImGui::GetStyle().FrameRounding, 0,
                              1 * ImGui::GetStyle().ScaleFactor);
        }

        return ret;
    }

    bool SearchInputText(ImGuiID idx, Skore::String& string, ImGuiInputTextFlags flags)
    {
        const auto searching = !string.Empty();

        auto&       style = ImGui::GetStyle();
        const float newPadding = 28.0f * ImGui::GetStyle().ScaleFactor;
        auto&       context = *ImGui::GetCurrentContext();
        auto*       drawList = ImGui::GetWindowDrawList();
        auto&       rect = context.LastItemData.Rect;

        ImGui::StyleVar styleVar{ImGuiStyleVar_FramePadding, ImVec2(newPadding, style.FramePadding.y)};

        auto modified = InputText(idx, string, flags);

        if (!searching)
        {
            drawList->AddText(ImVec2(rect.Min.x + newPadding, rect.Min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Search");
        }

        drawList->AddText(ImVec2(rect.Min.x + style.ItemInnerSpacing.x, rect.Min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_MAGNIFYING_GLASS);

        return modified;
    }

    void BeginTreeNode()
    {
       // ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, std::ceil(1 * ImGui::GetStyle().ScaleFactor)));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.67f)); //TODO - get from config (selected item)
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0, 0.0f, 0.0f, 0.0f));
    }

    void EndTreeNode()
    {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }

    bool TreeNode(u32 id, const char* label, ImGuiTreeNodeFlags flags)
    {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_SpanFullWidth |
                ImGuiTreeNodeFlags_FramePadding;
        return ImGui::TreeNodeEx((void*)(usize)id, flags, "%s", label);
    }

    bool TreeLeaf(u32 id, const char* label, ImGuiTreeNodeFlags flags)
    {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_SpanFullWidth |
            ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_FramePadding;

        return ImGui::TreeNodeEx((void*)(usize)id, flags, "%s", label);
    }

    void TextureItem(Texture texture, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
    {
        if (!texture) return;

        ImGui::Image(GetRenderDevice().GetImGuiTexture(texture), image_size, uv0, uv1, tint_col, border_col);
    }

    void DrawTexture(Texture texture, const Rect& rect, const ImVec4& tintCol)
    {
        if (!texture) return;

        ImDrawList* drawList = GetWindowDrawList();

        drawList->AddImage(
            GetRenderDevice().GetImGuiTexture(texture),
            ImVec2(rect.x, rect.y),
            ImVec2(rect.width, rect.height),
            ImVec2(0, 0),
            ImVec2(1, 1),
            ImGui::ColorConvertFloat4ToU32(tintCol));
    }

    bool BeginPopupMenu(const char* str, ImGuiWindowFlags popupFlags, bool setSize)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6 * style.ScaleFactor, 4 * style.ScaleFactor});
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{1, 1});

        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.67f)); //TODO - get from config (selected item)
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));

        if (setSize)
        {
            ImGui::SetNextWindowSize(ImVec2{300, 0,}, ImGuiCond_Once);
        }
        bool res = ImGui::BeginPopup(str, popupFlags);
        return res;
    }

    void EndPopupMenu(bool closePopup)
    {
        if (closePopup)
        {
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }

    bool SelectionButton(const char* label, bool selected, const ImVec2& sizeArg)
    {
        //TODO make select
        return Button(label, sizeArg);
    }


    bool BorderedButton(const char* label, const ImVec2& size)
    {
        ImGui::StyleColor border(ImGuiCol_Border, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));
        return ImGui::Button(label, size);
    }

    bool CollapsingHeaderProps(i32 id, const char* label, bool* buttonClicked)
    {
        auto& style = ImGui::GetStyle();

        ImGui::PushID(id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowItemOverlap;
        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
        bool   open = ImGui::CollapsingHeader(label, flags);
        bool   rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        bool   hovered = ImGui::IsItemHovered();
        ImVec2 size = ImGui::GetItemRectSize();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20 * style.ScaleFactor);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * style.ScaleFactor);
        {
            ImGui::StyleColor colBorder(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            if (hovered)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            }
            if (ImGui::Button(ICON_FA_ELLIPSIS_VERTICAL, ImVec2{size.y, size.y - 4 * style.ScaleFactor}) || rightClicked)
            {
                *buttonClicked = true;
            }
            if (hovered)
            {
                ImGui::PopStyleColor(1);
            }
        }
        ImGui::PopID();

        return open;
    }

    void DummyRect(ImVec2 min, ImVec2 max)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;

        const ImRect bb(min, max);
        ItemSize(max - min);
        ItemAdd(bb, 0);
    }

    ImVec2 GetParentWorkRectMin()
    {
        return GetCurrentWindow()->ParentWorkRect.Min;
    }
    ImVec2 GetParentWorkRectMax()
    {
        return GetCurrentWindow()->ParentWorkRect.Max;
    }

    ImU32 TextToColor(const char* str)
    {
        auto color = ImHashStr(str, strlen(str));
        auto vecColor = ImGui::ColorConvertU32ToFloat4(color);
        return IM_COL32(vecColor.x * 255, vecColor.y * 255, vecColor.z * 255, 255);
    }

    void RegisterKeys()
    {
        keys[static_cast<u32>(Key::Space)] = ImGuiKey_Space;
        keys[static_cast<u32>(Key::Apostrophe)] = ImGuiKey_Apostrophe;
        keys[static_cast<u32>(Key::Comma)] = ImGuiKey_Comma;
        keys[static_cast<u32>(Key::Minus)] = ImGuiKey_Minus;
        keys[static_cast<u32>(Key::Period)] = ImGuiKey_Period;
        keys[static_cast<u32>(Key::Slash)] = ImGuiKey_Slash;
        keys[static_cast<u32>(Key::Num0)] = ImGuiKey_0;
        keys[static_cast<u32>(Key::Num1)] = ImGuiKey_1;
        keys[static_cast<u32>(Key::Num2)] = ImGuiKey_2;
        keys[static_cast<u32>(Key::Num3)] = ImGuiKey_3;
        keys[static_cast<u32>(Key::Num4)] = ImGuiKey_4;
        keys[static_cast<u32>(Key::Num5)] = ImGuiKey_5;
        keys[static_cast<u32>(Key::Num6)] = ImGuiKey_6;
        keys[static_cast<u32>(Key::Num7)] = ImGuiKey_7;
        keys[static_cast<u32>(Key::Num8)] = ImGuiKey_8;
        keys[static_cast<u32>(Key::Num9)] = ImGuiKey_9;
        keys[static_cast<u32>(Key::Semicolon)] = ImGuiKey_Semicolon;
        keys[static_cast<u32>(Key::Equal)] = ImGuiKey_Equal;
        keys[static_cast<u32>(Key::A)] = ImGuiKey_A;
        keys[static_cast<u32>(Key::B)] = ImGuiKey_B;
        keys[static_cast<u32>(Key::C)] = ImGuiKey_C;
        keys[static_cast<u32>(Key::D)] = ImGuiKey_D;
        keys[static_cast<u32>(Key::E)] = ImGuiKey_E;
        keys[static_cast<u32>(Key::F)] = ImGuiKey_F;
        keys[static_cast<u32>(Key::G)] = ImGuiKey_G;
        keys[static_cast<u32>(Key::H)] = ImGuiKey_H;
        keys[static_cast<u32>(Key::I)] = ImGuiKey_I;
        keys[static_cast<u32>(Key::J)] = ImGuiKey_J;
        keys[static_cast<u32>(Key::K)] = ImGuiKey_K;
        keys[static_cast<u32>(Key::L)] = ImGuiKey_L;
        keys[static_cast<u32>(Key::M)] = ImGuiKey_M;
        keys[static_cast<u32>(Key::N)] = ImGuiKey_N;
        keys[static_cast<u32>(Key::O)] = ImGuiKey_O;
        keys[static_cast<u32>(Key::P)] = ImGuiKey_P;
        keys[static_cast<u32>(Key::Q)] = ImGuiKey_Q;
        keys[static_cast<u32>(Key::R)] = ImGuiKey_R;
        keys[static_cast<u32>(Key::S)] = ImGuiKey_S;
        keys[static_cast<u32>(Key::T)] = ImGuiKey_T;
        keys[static_cast<u32>(Key::U)] = ImGuiKey_U;
        keys[static_cast<u32>(Key::V)] = ImGuiKey_V;
        keys[static_cast<u32>(Key::W)] = ImGuiKey_W;
        keys[static_cast<u32>(Key::X)] = ImGuiKey_X;
        keys[static_cast<u32>(Key::Y)] = ImGuiKey_Y;
        keys[static_cast<u32>(Key::Z)] = ImGuiKey_Z;
        keys[static_cast<u32>(Key::LeftBracket)] = ImGuiKey_LeftBracket;
        keys[static_cast<u32>(Key::Backslash)] = ImGuiKey_Backslash;
        keys[static_cast<u32>(Key::RightBracket)] = ImGuiKey_RightBracket;
        keys[static_cast<u32>(Key::GraveAccent)] = ImGuiKey_GraveAccent;
        //		keys[static_cast<u32>(Key::World1)] = ImGuiKey_World1;
        //		keys[static_cast<u32>(Key::World2)] = ImGuiKey_World2;
        keys[static_cast<u32>(Key::Escape)] = ImGuiKey_Escape;
        keys[static_cast<u32>(Key::Enter)] = ImGuiKey_Enter;
        keys[static_cast<u32>(Key::Tab)] = ImGuiKey_Tab;
        keys[static_cast<u32>(Key::Backspace)] = ImGuiKey_Backspace;
        keys[static_cast<u32>(Key::Insert)] = ImGuiKey_Insert;
        keys[static_cast<u32>(Key::Delete)] = ImGuiKey_Delete;
        keys[static_cast<u32>(Key::Right)] = ImGuiKey_RightArrow;
        keys[static_cast<u32>(Key::Left)] = ImGuiKey_LeftArrow;
        keys[static_cast<u32>(Key::Down)] = ImGuiKey_DownArrow;
        keys[static_cast<u32>(Key::Up)] = ImGuiKey_UpArrow;
        keys[static_cast<u32>(Key::PageUp)] = ImGuiKey_PageUp;
        keys[static_cast<u32>(Key::PageDown)] = ImGuiKey_PageDown;
        keys[static_cast<u32>(Key::Home)] = ImGuiKey_Home;
        keys[static_cast<u32>(Key::End)] = ImGuiKey_End;
        keys[static_cast<u32>(Key::CapsLock)] = ImGuiKey_CapsLock;
        keys[static_cast<u32>(Key::ScrollLock)] = ImGuiKey_ScrollLock;
        keys[static_cast<u32>(Key::NumLock)] = ImGuiKey_NumLock;
        keys[static_cast<u32>(Key::PrintScreen)] = ImGuiKey_PrintScreen;
        keys[static_cast<u32>(Key::Pause)] = ImGuiKey_Pause;
        keys[static_cast<u32>(Key::F1)] = ImGuiKey_F1;
        keys[static_cast<u32>(Key::F2)] = ImGuiKey_F2;
        keys[static_cast<u32>(Key::F3)] = ImGuiKey_F3;
        keys[static_cast<u32>(Key::F4)] = ImGuiKey_F4;
        keys[static_cast<u32>(Key::F5)] = ImGuiKey_F5;
        keys[static_cast<u32>(Key::F6)] = ImGuiKey_F6;
        keys[static_cast<u32>(Key::F7)] = ImGuiKey_F7;
        keys[static_cast<u32>(Key::F8)] = ImGuiKey_F8;
        keys[static_cast<u32>(Key::F9)] = ImGuiKey_F9;
        keys[static_cast<u32>(Key::F10)] = ImGuiKey_F10;
        keys[static_cast<u32>(Key::F11)] = ImGuiKey_F11;
        keys[static_cast<u32>(Key::F12)] = ImGuiKey_F12;
        keys[static_cast<u32>(Key::F13)] = ImGuiKey_F13;
        keys[static_cast<u32>(Key::F14)] = ImGuiKey_F14;
        keys[static_cast<u32>(Key::F15)] = ImGuiKey_F15;
        keys[static_cast<u32>(Key::F16)] = ImGuiKey_F16;
        keys[static_cast<u32>(Key::F17)] = ImGuiKey_F17;
        keys[static_cast<u32>(Key::F18)] = ImGuiKey_F18;
        keys[static_cast<u32>(Key::F19)] = ImGuiKey_F19;
        keys[static_cast<u32>(Key::F20)] = ImGuiKey_F20;
        keys[static_cast<u32>(Key::F21)] = ImGuiKey_F21;
        keys[static_cast<u32>(Key::F22)] = ImGuiKey_F22;
        keys[static_cast<u32>(Key::F23)] = ImGuiKey_F23;
        keys[static_cast<u32>(Key::F24)] = ImGuiKey_F24;
        keys[static_cast<u32>(Key::Keypad0)] = ImGuiKey_Keypad0;
        keys[static_cast<u32>(Key::Keypad1)] = ImGuiKey_Keypad1;
        keys[static_cast<u32>(Key::Keypad2)] = ImGuiKey_Keypad2;
        keys[static_cast<u32>(Key::Keypad3)] = ImGuiKey_Keypad3;
        keys[static_cast<u32>(Key::Keypad4)] = ImGuiKey_Keypad4;
        keys[static_cast<u32>(Key::Keypad5)] = ImGuiKey_Keypad5;
        keys[static_cast<u32>(Key::Keypad6)] = ImGuiKey_Keypad6;
        keys[static_cast<u32>(Key::Keypad7)] = ImGuiKey_Keypad7;
        keys[static_cast<u32>(Key::Keypad8)] = ImGuiKey_Keypad8;
        keys[static_cast<u32>(Key::Keypad9)] = ImGuiKey_Keypad9;
        keys[static_cast<u32>(Key::KeypadDecimal)] = ImGuiKey_KeypadDecimal;
        keys[static_cast<u32>(Key::KeypadDivide)] = ImGuiKey_KeypadDivide;
        keys[static_cast<u32>(Key::KeypadMultiply)] = ImGuiKey_KeypadMultiply;
        keys[static_cast<u32>(Key::KeypadSubtract)] = ImGuiKey_KeypadSubtract;
        keys[static_cast<u32>(Key::KeypadAdd)] = ImGuiKey_KeypadAdd;
        keys[static_cast<u32>(Key::KeypadEnter)] = ImGuiKey_KeypadEnter;
        keys[static_cast<u32>(Key::KeypadEqual)] = ImGuiKey_KeypadEqual;
        keys[static_cast<u32>(Key::LeftShift)] = ImGuiKey_LeftShift;
        keys[static_cast<u32>(Key::LeftCtrl)] = ImGuiKey_LeftCtrl;
        keys[static_cast<u32>(Key::LeftAlt)] = ImGuiKey_LeftAlt;
        keys[static_cast<u32>(Key::LeftSuper)] = ImGuiKey_LeftSuper;
        keys[static_cast<u32>(Key::RightShift)] = ImGuiKey_RightShift;
        keys[static_cast<u32>(Key::RightCtrl)] = ImGuiKey_RightCtrl;
        keys[static_cast<u32>(Key::RightAlt)] = ImGuiKey_RightAlt;
        keys[static_cast<u32>(Key::RightSuper)] = ImGuiKey_RightSuper;
        keys[static_cast<u32>(Key::Menu)] = ImGuiKey_Menu;
    }

    void ApplyDefaultStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4*     colors = style.Colors;

        colors[ImGuiCol_Text] = ImVec4(0.71f, 0.72f, 0.71f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        colors[ImGuiCol_Border] = ImVec4(0.01f, 0.01f, 0.02f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.42f, 0.42f, 0.42f, 0.40f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.67f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.11f, 0.12f, 0.13f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.59f, 0.60f, 0.59f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.59f, 0.60f, 0.59f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.64f, 0.64f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
        colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.01f, 0.02f, 0.04f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.36f, 0.46f, 0.54f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.55f, 0.78f, 1.00f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.58f, 0.71f, 0.82f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_DockingPreview] = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.50f, 0.50f, 0.50f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);

        style.PopupRounding = 3;

        style.WindowPadding = ImVec2(6, 6);
        style.FramePadding = ImVec2(5, 4);
        style.ItemSpacing = ImVec2(8, 2);
        style.CellPadding = ImVec2(4, 1);
        style.ScrollbarSize = 15;
        style.WindowBorderSize = 1;
        style.ChildBorderSize = 0;
        style.PopupBorderSize = 1;
        style.FrameBorderSize = 1;
        style.WindowRounding = 3;
        style.ChildRounding = 0;
        style.FrameRounding = 3;
        style.ScrollbarRounding = 2;
        style.GrabRounding = 3;

        style.TabBorderSize = 0;
        style.TabRounding = 2;
        style.IndentSpacing = 10;

        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);

        colors[ImGuiCol_Tab] = colors[ImGuiCol_TitleBg];
        colors[ImGuiCol_TabHovered] = colors[ImGuiCol_WindowBg];
        colors[ImGuiCol_TabActive] = colors[ImGuiCol_WindowBg];
        colors[ImGuiCol_TabUnfocused] = colors[ImGuiCol_TitleBg];
        colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_WindowBg];
        colors[ImGuiCol_DockingPreview] = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
        style.ScaleAllSizes(scaleFactor);

        //guizmo
        f32   guizmoScaleFactor = scaleFactor * 1.1;
        auto& guizmoSize = ImGuizmo::GetStyle();
        new(&guizmoSize) ImGuizmo::Style{};
        //
        guizmoSize.CenterCircleSize = guizmoSize.CenterCircleSize * guizmoScaleFactor;
        guizmoSize.HatchedAxisLineThickness = guizmoSize.HatchedAxisLineThickness * guizmoScaleFactor;
        guizmoSize.RotationLineThickness = guizmoSize.RotationLineThickness * guizmoScaleFactor;
        guizmoSize.RotationOuterLineThickness = guizmoSize.RotationOuterLineThickness * guizmoScaleFactor;
        guizmoSize.ScaleLineCircleSize = guizmoSize.ScaleLineCircleSize * guizmoScaleFactor;
        guizmoSize.ScaleLineThickness = guizmoSize.ScaleLineThickness * guizmoScaleFactor;
        guizmoSize.TranslationLineArrowSize = guizmoSize.TranslationLineArrowSize * guizmoScaleFactor;
        guizmoSize.TranslationLineThickness = guizmoSize.TranslationLineThickness * guizmoScaleFactor;
    }

    void ApplyFonts()
    {
        f32 fontSize = 15.f;

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        {
            Array<u8> bytes = StaticContent::GetBinaryFile("Content/Fonts/DejaVuSans.ttf");
            auto font = ImFontConfig();
            font.SizePixels = fontSize * scaleFactor;
            memcpy(font.Name, "DejaVuSans", 9);
            font.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryTTF(bytes.Data(), bytes.Size(), font.SizePixels, &font);
        }

        {
            static constexpr ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

            ImFontConfig config = ImFontConfig();
            config.SizePixels = fontSize * scaleFactor;
            config.MergeMode = true;
            config.GlyphMinAdvanceX = fontSize * scaleFactor;
            config.GlyphMaxAdvanceX = fontSize * scaleFactor;
            config.FontDataOwnedByAtlas = false;
            memcpy(config.Name, "FontAwesome", 11);

            Array<u8> bytes = StaticContent::GetBinaryFile("Content/Fonts/fa-solid-900.otf");
            io.Fonts->AddFontFromMemoryTTF(bytes.Data(), bytes.Size(), config.SizePixels, &config, icon_ranges);
        }
    }

    void Init(Skore::Window window, Skore::Swapchain swapchain)
    {
        scaleFactor = Platform::GetWindowScale(window);
        RegisterKeys();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = "imgui_impl_skore";
        io.BackendRendererName = "imgui_impl_skore";
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigViewportsNoTaskBarIcon = true;

        ApplyDefaultStyle();
        ApplyFonts();

        Platform::ImGuiInit(window);
        GetRenderDevice().ImGuiInit(swapchain);
    }



    void BeginFrame(Window window, f64 deltaTime)
    {
        GetRenderDevice().ImGuiNewFrame();
        Platform::ImGuiNewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }



    void Render(RenderCommands& renderCommands)
    {
        Render();
        GetRenderDevice().ImGuiRender(renderCommands);
    }

    void ImGuiShutdown()
    {
        DestroyContext();
    }

    ImGuiKey GetImGuiKey(Key key)
    {
        return keys[static_cast<usize>(key)];
    }

    bool CurrentTableHovered()
    {
         auto currentTable = ImGui::GetCurrentTable();
        return currentTable && ImGui::TableGetHoveredRow() == currentTable->CurrentRow;
    }
}
