
#pragma once

#include "Skore/EditorCommon.hpp"


namespace Skore
{
	class GPUTexture;

	class TextureViewWindow : public EditorWindow
	{
	public:
		SK_CLASS(TextureViewWindow, EditorWindow);


		void Init(u32 id, VoidPtr userData) override;
		void Draw(u32 id, bool& open) override;

		static void Open(GPUTexture* texture);

	private:
		GPUTexture* texture = nullptr;
	};
}
