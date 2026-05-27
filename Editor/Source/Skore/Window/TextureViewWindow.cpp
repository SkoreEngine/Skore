#include "Skore/Window/TextureViewWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/ImGui.hpp"


namespace Skore
{
	void TextureViewWindow::Init(VoidPtr userData)
	{
		texture = static_cast<GPUTexture*>(userData);
	}

	void TextureViewWindow::Draw(bool& open)
	{
		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar);
		ImGuiTextureItem(texture, ImGui::GetWindowSize());
		ImGui::End();
	}

	void TextureViewWindow::Open(GPUTexture* texture)
	{
		Editor::GetActiveWorkspace()->OpenWindow<TextureViewWindow>(texture);
	}
}
