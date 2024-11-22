#include "SettingsWindow.hpp"

#include "SceneViewWindow.hpp"
#include "Fyrion/Core/StringUtils.hpp"

#include "Fyrion/Editor/Editor.hpp"
#include "Fyrion/ImGui/ImGui.hpp"

namespace Fyrion
{
    void SettingsWindow::Init(u32 id, VoidPtr userData)
    {
        type = reinterpret_cast<TypeID>(userData);
        if (TypeHandler* typeHandler = Registry::FindTypeById(type))
        {
            title = FormatName(typeHandler->GetSimpleName());
        }
    }

    void SettingsWindow::Draw(u32 id, bool& open)
    {
        auto&  style = ImGui::GetStyle();
        ImVec2 padding = style.WindowPadding;

        ImGui::StyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::StyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));

        ImGui::CenterWindow(ImGuiCond_Appearing);
        ImGui::Begin(id, title.CStr(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);

        if (ImGui::BeginTable("settings-windows-table", 2, ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 300 * style.ScaleFactor);
            ImGui::TableNextColumn();

            ImGui::StyleVar childPadding(ImGuiStyleVar_WindowPadding, padding);
            DrawTree();
            ImGui::TableNextColumn();
            DrawSelected();

            ImGui::EndTable();
        }

        ImGui::End();
    }

    void SettingsWindow::Open(const MenuItemEventData& eventData)
    {
        Editor::OpenWindow<SettingsWindow>(reinterpret_cast<VoidPtr>(eventData.userData));
    }

    void SettingsWindow::DrawTree()
    {
        //left panel
        ImGui::StyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
        ImGui::BeginChild(4000, ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);

        ImGui::SetNextItemWidth(-1);
        ImGui::SearchInputText(4001, searchText);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * ImGui::GetStyle().ScaleFactor);

        ImGui::BeginTreeNode();

        ImGui::TreeLeaf(40001, "test1");
        ImGui::TreeLeaf(40002, "test2");

        ImGui::EndTreeNode();

        ImGui::EndChild();
    }

    void SettingsWindow::DrawSelected()
    {
        ImGui::BeginChild(5000);





        ImGui::EndChild();
    }
}
