#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"

namespace Skore
{
	class SceneEditor;
	class ReflectField;
	struct MenuItemEventData;
	class Entity;
	class Scene;
	class RenderPipelineContext;

	class PropertiesWindow : public EditorWindow
	{
	public:
		SK_CLASS(PropertiesWindow, EditorWindow);

		PropertiesWindow();
		~PropertiesWindow() override;
		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;
		void        Render(GPUCommandBuffer* cmd) override;

		static void RegisterType(NativeReflectType<PropertiesWindow>& type);

	private:
		enum class PreviewMode
		{
			Scene,
			Texture
		};

		String stringCache{};
		RID    selectedEntity{};
		bool   renamingFocus{};
		String renamingCache{};
		RID    renamingEntity{};
		String searchComponentString{};
		RID    selectedComponent = {};
		u32    selectedComponentIndex = U32_MAX;
		RID    selectedAsset = {};
		RID    selectedResource = {};
		u64   importSettingsVersion = U64_MAX;

		Entity* selectedDebugEntity = nullptr;

		f32 m_previewHeight = 0;

		PreviewMode m_mode = PreviewMode::Scene;
		bool        m_previewVisible = false;
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

		void ClearSelection();

		void DrawPreview();
		void DrawScene();
		void DrawTexture();
		void DrawTextureToolbar();
		void DrawTextureViewport();
		void DrawTextureInfo();

		void SetAsset(RID asset);
		void SetTexture(RID texture);
		void RefreshPreview();
		void RecreateScene();
		void RebuildScene();
		void FrameCamera();
		void EnsureContext(Extent extent);
		void RebuildTextureResources();
		void ReleaseTextureResources();

		static void OpenProperties(const MenuItemEventData& eventData);

		void DrawEntity(u32 id, SceneEditor* sceneEditor, RID entity);
		void DrawDebugEntity(u32 id, SceneEditor* sceneEditor, Entity* entity);
		void DrawAsset(u32 id, RID asset);
		void DrawResource(u32 id, RID resource);

		void EntityDebugSelection(u32 workspaceId, Entity* entity);
		void EntityDebugDeselection(u32 workspaceId, Entity* entity);

		void EntitySelection(u32 workspaceId, RID entityId);
		void EntityDeselection(u32 workspaceId, RID entityId);

		void AssetSelection(u32 workspaceId, RID assetId, RID previewId);
		void ResourceSelection(u32 workspaceId, RID resourceId);
	};
}
