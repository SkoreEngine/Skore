
#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"


namespace Skore
{
	class TextureViewWindow : public EditorWindow
	{
	public:
		SK_CLASS(TextureViewWindow, EditorWindow);

		~TextureViewWindow() override;

		const char* GetTitle() const override { return "Texture View"; }
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void Open(RID texture);

	private:
		RID                     textureRID = {};
		TextureResourceCachePtr textureCache = {};

		GPUTexture*     texture = nullptr;
		GPUTextureView* textureView = nullptr;
		GPUSampler*     sampler = nullptr;

		u32  mipLevel = 0;
		f32  zoom = 1.0f;
		Vec2 pan = {0, 0};
		bool fitRequested = true;
		bool resourcesDirty = true;

		FilterMode  filterMode = FilterMode::Linear;
		AddressMode addressMode = AddressMode::Repeat;

		void RebuildResources();
		void ReleaseResources();
		void WriteSamplerSettings();
		void DrawToolbar();
		void DrawViewport();
		void DrawProperties();
	};
}
