#include "TextureViewWindow.hpp"

#include "Skore/Editor/Editor.hpp"
#include "Skore/ImGui/ImGui.hpp"


namespace Skore
{

    void TextureViewWindow::Init(u32 id, VoidPtr userData)
    {
        texture = *static_cast<Texture*>(userData);
    }


    void TextureViewWindow::Draw(u32 id, bool& open)
    {
        ImGui::Begin(id, "Texture View", &open, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextureItem(texture, ImGui::GetWindowSize());
        ImGui::End();
    }



    void TextureViewWindow::Open(Texture texture)
    {
        Editor::OpenWindow<TextureViewWindow>(&texture);
    }
}
