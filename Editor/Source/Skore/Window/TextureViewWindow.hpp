
#pragma once

#include "Skore/EditorCommon.hpp"


namespace Skore
{
	class GPUTexture;

	class TextureViewWindow : public EditorWindow
	{
	public:
		SK_CLASS(TextureViewWindow, EditorWindow);


		const char* GetTitle() const override { return "Texture View"; }
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void Open(GPUTexture* texture);

	private:
		GPUTexture* texture = nullptr;
	};
}
