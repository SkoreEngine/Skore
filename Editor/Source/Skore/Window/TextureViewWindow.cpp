#include "Skore/Window/TextureViewWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/ImGui.hpp"


namespace Skore
{
	void TextureViewWindow::Init(u32 id, VoidPtr userData)
	{
		texture = static_cast<GPUTexture*>(userData);
	}

	void TextureViewWindow::Draw(u32 id, bool& open)
	{
		ImGuiBegin(id, "Texture View", &open, ImGuiWindowFlags_NoScrollbar);
		ImGuiTextureItem(texture, ImGui::GetWindowSize());
		ImGui::End();
	}

	void TextureViewWindow::Open(GPUTexture* texture)
	{
		Editor::GetActiveWorkspace()->OpenWindow<TextureViewWindow>(texture);
	}
}
