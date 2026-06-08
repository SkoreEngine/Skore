#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"

namespace Skore
{
	class Scene;
	class RenderPipelineContext;
	class PreviewGenerator;
	class EditorWorkspace;

	enum class PreviewMode
	{
		Scene,
		Texture
	};

	class PreviewWindow : public EditorWindow
	{
	public:
		SK_CLASS(PreviewWindow, EditorWindow);

		PreviewWindow() = default;
		~PreviewWindow() override;

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;
		void        Render(GPUCommandBuffer* cmd) override;

		Scene* GetScene() const { return m_scene; }

		void SetPreview(PreviewGenerator& generator);
		void SetAsset(RID asset);
		void SetTexture(RID texture);
		void Clear();
		void FrameCamera();

		static void OpenAsset(RID asset);
		static void OpenTexture(RID texture);
		static void Refresh();

		static void RegisterType(NativeReflectType<PreviewWindow>& type);

	private:
		PreviewMode m_mode = PreviewMode::Scene;
		bool        m_focusRequested = false;

		Scene*                 m_scene = nullptr;
		RenderPipelineContext* m_context = nullptr;

		bool m_framePending = false;

		f32  m_orbitYaw = 45.0f;
		f32  m_orbitPitch = 30.0f;
		f32  m_orbitDistance = 7.0f;
		Vec3 m_orbitTarget = {};
		f32  m_fov = 60.0f;

		Extent m_outputSize = {512, 512};

		RID m_sceneAsset = {};

		RID                     m_textureRID = {};
		TextureResourceCachePtr m_textureCache = {};
		GPUTexture*             m_texture = nullptr;
		GPUTextureView*         m_textureView = nullptr;
		GPUSampler*             m_textureSampler = nullptr;
		u32                     m_mipLevel = 0;
		f32                     m_textureZoom = 1.0f;
		Vec2                    m_texturePan = {0, 0};
		bool                    m_fitRequested = true;
		bool                    m_textureResourcesDirty = true;
		u64                     m_textureVersion = 0;

		void RecreateScene();
		void RebuildScene();
		void EnsureContext(Extent extent);
		void DrawScene();
		void DoRefresh();

		static PreviewWindow* FindPreview(EditorWorkspace* workspace);
		static PreviewWindow* GetOrCreate(EditorWorkspace* workspace);

		void RebuildTextureResources();
		void ReleaseTextureResources();
		void DrawTexture();
		void DrawTextureToolbar();
		void DrawTextureViewport();
		void DrawTextureInfo();

		static void OpenPreview(const MenuItemEventData& eventData);
	};
}
