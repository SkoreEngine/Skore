#include "SettingsWindow.hpp"

#include "SceneViewWindow.hpp"
#include "Skore/Core/SettingsManager.hpp"
#include "Skore/Core/StringUtils.hpp"

#include "Skore/Editor/Editor.hpp"
#include "Skore/Editor/ImGui/ImGuiEditor.hpp"
#include "Skore/ImGui/ImGui.hpp"

namespace Skore
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

        ImGui::StyleVar   windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
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

        for (SettingsItem* item : SettingsManager::GetItems(type))
        {
            DrawItem(item);
        }

        ImGui::EndTreeNode();

        ImGui::EndChild();
    }

    void SettingsWindow::DrawItem(SettingsItem* settingsItem)
    {
        Span<SettingsItem*> children = settingsItem->GetChildren();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
        if (selectedItem == settingsItem)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        bool open = false;

        if (!children.Empty())
        {
            open = ImGui::TreeNode(HashInt32(reinterpret_cast<usize>(settingsItem)), settingsItem->GetLabel().CStr(), flags);
        }
        else
        {
            ImGui::TreeLeaf(HashInt32(reinterpret_cast<usize>(settingsItem)), settingsItem->GetLabel().CStr(), flags);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            selectedItem = settingsItem;
        }

        if (open)
        {
            for (SettingsItem* item : children)
            {
                DrawItem(item);
            }

            ImGui::TreePop();
        }
    }
}

void SettingsWindow::DrawSelected()
{
    ImGui::BeginChild(5000, ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);

    if (selectedItem != nullptr && selectedItem->GetInstance() != nullptr)
    {
        ImGui::DrawType(ImGui::DrawTypeDesc{
            .itemId = HashInt32(reinterpret_cast<usize>(selectedItem)),
            .typeHandler = selectedItem->GetTypeHandler(),
            .instance = selectedItem->GetInstance(),
            .userData = selectedItem,
            .callback = [](ImGui::DrawTypeDesc& desc)
            {

            }
        });
    }

    ImGui::EndChild();
}
