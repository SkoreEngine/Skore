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
	class PreviewGenerator;
	enum class MaterialNodePropertyType : u8;

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
		RID    selectedMaterialNode = {};

		//transient material-node edit state so an in-progress value drag or name edit isn't clobbered
		//by the per-frame resource read; the live value is held here until the widget deactivates.
		bool   m_nodeValueEditing = false;
		RID    m_nodeValueNode = {};
		Vec4   m_nodeValueEdit = {};
		bool   m_nodeNameFocus = false;
		RID    m_nodeNameNode = {};
		String m_nodeNameEdit = {};

		//transient material-instance override edit state (same rationale as the node edit state above);
		//keyed by the override entry RID so a drag survives the per-frame read.
		bool   m_instanceValueEditing = false;
		RID    m_instanceValueEntry = {};
		Vec4   m_instanceValueEdit = {};
		RID    m_instanceTextureEntry = {}; //override entry targeted by the texture-selection popup

		RID    importSettingsSource = {};
		RID    importSettingsDraft = {};
		RID    importSettingsPendingApply = {};
		u64    importSettingsVersion = U64_MAX;
		u64    importSettingsSourceVersion = U64_MAX;

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
		f32  m_sceneRadius = 7.0f;
		Vec3 m_orbitTarget = {};
		f32  m_fov = 60.0f;

		Extent m_outputSize = {512, 512};

		PreviewGenerator* m_previewGenerator = nullptr;

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

		void SetPreview(RID asset, TypeID previewType);
		void SetTexture(RID texture);
		void RefreshPreview();
		void RecreateScene();
		void RebuildScene();
		void DestroyPreviewGenerator();
		void FrameCamera();
		void EnsureContext(Extent extent);
		void RebuildTextureResources();
		void ReleaseTextureResources();
		void ClearImportSettingsDraft();
		RID  GetImportSettingsDraft(RID importSettings);

		static void OpenProperties(const MenuItemEventData& eventData);

		void DrawEntity(u32 id, SceneEditor* sceneEditor, RID entity);
		void DrawDebugEntity(u32 id, SceneEditor* sceneEditor, Entity* entity);
		void DrawAsset(u32 id, RID asset);
		void DrawResource(u32 id, RID resource);
		void DrawMaterialNode(u32 id, RID node);
		void DrawMaterialOutputProperties(RID graph);
		void DrawNodeTextureProperty(u64 id, RID node);
		void DrawNodeNameProperty(u64 id, RID node);
		void DrawNodeValueProperty(u64 id, RID node, MaterialNodePropertyType type);
		void SetNodeTexture(RID node, RID texture);

		void DrawMaterialInstance(u32 id, RID instance);
		void DrawInstanceParentProperty(u64 id, RID instance, RID parent);
		void DrawInstanceScalarProperty(u64 id, RID entry, Vec4 value, MaterialNodePropertyType type);
		void DrawInstanceTextureProperty(u64 id, RID entry, RID texture);
		void SetInstanceTexture(RID entry, RID texture);

		void EntityDebugSelection(u32 workspaceId, Entity* entity);
		void EntityDebugDeselection(u32 workspaceId, Entity* entity);

		void EntitySelection(u32 workspaceId, RID entityId);
		void EntityDeselection(u32 workspaceId, RID entityId);

		void AssetSelection(u32 workspaceId, RID assetId);
		void ResourceSelection(u32 workspaceId, RID resourceId);
		void MaterialNodeSelection(u32 workspaceId, RID nodeId);
	};
}
