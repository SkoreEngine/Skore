#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"

namespace Skore
{
	class Scene;
	class RenderPipelineContext;
	class PreviewGenerator;

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
		void Clear();
		void FrameCamera();

		static void RegisterType(NativeReflectType<PreviewWindow>& type);

	private:
		Scene*                 m_scene = nullptr;
		RenderPipelineContext* m_context = nullptr;

		bool m_framePending = false;

		f32  m_orbitYaw = 45.0f;
		f32  m_orbitPitch = 30.0f;
		f32  m_orbitDistance = 7.0f;
		Vec3 m_orbitTarget = {};
		f32  m_fov = 60.0f;

		Extent m_outputSize = {512, 512};

		void RecreateScene();
		void EnsureContext(Extent extent);

		static void OpenPreview(const MenuItemEventData& eventData);
	};
}
