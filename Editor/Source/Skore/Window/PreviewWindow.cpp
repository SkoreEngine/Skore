#include "Skore/Window/PreviewWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

namespace Skore
{
	struct PreviewRenderPipeline;

	struct PreviewOutputModule : RenderPipelineModule
	{
		SK_CLASS(PreviewOutputModule, RenderPipelineModule);

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = OutputColorName, .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8B8A8_UNORM, .samples = 1});
			return resources;
		}

		RenderPipelineModuleSetup GetSetup() override
		{
			return {};
		}
	};

	PreviewWindow::~PreviewWindow()
	{
		if (m_context)
		{
			m_context->Destroy();
			m_context = nullptr;
		}
		if (m_scene)
		{
			DestroyAndFree(m_scene);
			m_scene = nullptr;
		}
	}

	const char* PreviewWindow::GetTitle() const
	{
		return "Preview";
	}

	void PreviewWindow::Init(VoidPtr userData)
	{
		RecreateScene();
		PreviewGenerator::SetupDefaultEnvironment(m_scene);
		m_scene->ExecuteEvents(false);
		m_framePending = true;
	}

	void PreviewWindow::RecreateScene()
	{
		if (m_scene)
		{
			DestroyAndFree(m_scene);
			m_scene = nullptr;
		}

		m_scene = Alloc<Scene>();
		m_scene->renderObjects.asyncLoad = false;
		m_scene->renderObjects.requireTlas = false;
	}

	void PreviewWindow::SetPreview(PreviewGenerator& generator)
	{
		RecreateScene();
		generator.PopulateScene(m_scene);
		m_scene->ExecuteEvents(false);
		m_framePending = true;
	}

	void PreviewWindow::Clear()
	{
		RecreateScene();
		PreviewGenerator::SetupDefaultEnvironment(m_scene);
		m_scene->ExecuteEvents(false);
		m_framePending = true;
	}

	void PreviewWindow::FrameCamera()
	{
		if (!m_scene) return;

		f32 radius = 7.0f;
		m_orbitTarget = {};

		if (AABB aabb = m_scene->GetBounds())
		{
			m_orbitTarget = aabb.GetCenter();
			radius = aabb.GetRadius();
		}

		m_orbitDistance = radius / tan(Math::Radians(m_fov) * 0.70f);
	}

	void PreviewWindow::EnsureContext(Extent extent)
	{
		if (m_context == nullptr)
		{
			RenderPipelineContextSettings settings;
			settings.initialOutputSize = extent;
			settings.userData = this;

			Array<TypeID> modules;
			modules.EmplaceBack(sktypeid(PreviewOutputModule));

			m_context = RenderPipeline::CreateContext(sktypeid(PreviewRenderPipeline), modules, settings);
			m_context->SetColorOutput(OutputColorName);
		}
		else
		{
			m_context->SetOutputSize(extent);
		}

		m_outputSize = extent;
	}

	void PreviewWindow::Render(GPUCommandBuffer* cmd)
	{
		if (m_context == nullptr || m_scene == nullptr)
		{
			return;
		}

		m_scene->renderObjects.DoUpdate(cmd);

		if (m_framePending)
		{
			FrameCamera();
			m_framePending = false;
		}

		f32 elevation = Math::Radians(m_orbitPitch);
		f32 azimuth = Math::Radians(m_orbitYaw);

		Vec3 cameraOffset = Vec3(
			m_orbitDistance * Math::Cos(elevation) * Math::Sin(azimuth),
			m_orbitDistance * Math::Sin(elevation),
			m_orbitDistance * Math::Cos(elevation) * Math::Cos(azimuth)
		);

		Vec3 cameraPos = m_orbitTarget + cameraOffset;

		Quat pitchRotation = Quat::AngleAxis(-elevation, Vec3{1.0f, 0.0f, 0.0f});
		Quat yawRotation = Quat::AngleAxis(azimuth, Vec3{0.0f, 1.0f, 0.0f});
		Quat cameraRotation = Quat::Normalized(yawRotation * pitchRotation);

		Mat4 view = Mat4::Inverse(Mat4::Translate(Mat4{1.0}, cameraPos) * Quat::ToMatrix4(cameraRotation));

		Vec2 nearFar = {0.1f, 100.0f};
		if (AABB aabb = m_scene->GetBounds())
		{
			nearFar = Math::GerNearFarFromAABB(aabb, view);
		}

		m_context->UpdateCamera(nearFar.x, nearFar.y, m_fov, Projection::Perspective, view, cameraPos);
		m_context->Execute(cmd, m_scene);
	}

	void PreviewWindow::Draw(bool& open)
	{
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar);

		Vec2 avail = FromImVec2(ImGui::GetContentRegionAvail());

		if (avail.x > 0 && avail.y > 0)
		{
			EnsureContext(Extent{static_cast<u32>(avail.x), static_cast<u32>(avail.y)});

			if (GPUTexture* color = m_context->GetColorOutput())
			{
				ImGuiTextureItem(color, ImVec2(avail.x, avail.y));

				if (ImGui::IsItemHovered())
				{
					ImGuiIO& io = ImGui::GetIO();

					if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
					{
						m_orbitYaw -= io.MouseDelta.x * 0.5f;
						m_orbitPitch += io.MouseDelta.y * 0.5f;
						m_orbitPitch = Math::Clamp(m_orbitPitch, -89.0f, 89.0f);
					}

					if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
					{
						f32  elevation = Math::Radians(m_orbitPitch);
						f32  azimuth = Math::Radians(m_orbitYaw);
						Quat rotation = Quat::Normalized(Quat::AngleAxis(azimuth, Vec3{0, 1, 0}) * Quat::AngleAxis(-elevation, Vec3{1, 0, 0}));

						Vec3 right = rotation * Vec3(1, 0, 0);
						Vec3 up = Vec3(0, 1, 0);

						m_orbitTarget -= right * (io.MouseDelta.x * 0.005f * m_orbitDistance);
						m_orbitTarget += up * (io.MouseDelta.y * 0.005f * m_orbitDistance);
					}

					if (io.MouseWheel != 0.0f)
					{
						m_orbitDistance -= io.MouseWheel * m_orbitDistance * 0.1f;
						m_orbitDistance = Math::Clamp(m_orbitDistance, 0.1f, 10000.0f);
					}
				}
			}
		}

		ImGui::End();
	}

	void PreviewWindow::OpenPreview(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<PreviewWindow>();
	}

	void PreviewWindow::RegisterType(NativeReflectType<PreviewWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Preview", .action = OpenPreview});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightBottom,
			.workspaceTypes = {WorkspaceTypes::Scene}
		});
	}

	void RegisterPreviewWindow()
	{
		Reflection::Type<PreviewOutputModule>();
		Reflection::Type<PreviewWindow>();
	}
}
