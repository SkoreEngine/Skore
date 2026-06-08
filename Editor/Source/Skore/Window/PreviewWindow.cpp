#include "Skore/Window/PreviewWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

namespace Skore
{
	struct PreviewRenderPipeline;

	namespace
	{
		u32 MipSize(u32 base, u32 mip)
		{
			return Math::Max(1u, base >> mip);
		}

		void DrawChecker(ImDrawList* drawList, const ImVec2& imgMin, const ImVec2& imgMax, const ImVec2& clipMin, const ImVec2& clipMax)
		{
			const f32 cell = 16.0f;

			ImVec2 min = ImVec2(Math::Max(imgMin.x, clipMin.x), Math::Max(imgMin.y, clipMin.y));
			ImVec2 max = ImVec2(Math::Min(imgMax.x, clipMax.x), Math::Min(imgMax.y, clipMax.y));
			if (min.x >= max.x || min.y >= max.y) return;

			drawList->AddRectFilled(min, max, IM_COL32(48, 48, 48, 255));

			i32 startX = static_cast<i32>((min.x - imgMin.x) / cell);
			i32 startY = static_cast<i32>((min.y - imgMin.y) / cell);

			for (i32 yi = startY;; ++yi)
			{
				f32 y0 = imgMin.y + yi * cell;
				if (y0 >= max.y) break;

				for (i32 xi = startX;; ++xi)
				{
					f32 x0 = imgMin.x + xi * cell;
					if (x0 >= max.x) break;
					if (((xi + yi) & 1) == 0) continue;

					ImVec2 a = ImVec2(Math::Max(x0, min.x), Math::Max(y0, min.y));
					ImVec2 b = ImVec2(Math::Min(x0 + cell, max.x), Math::Min(y0 + cell, max.y));
					drawList->AddRectFilled(a, b, IM_COL32(64, 64, 64, 255));
				}
			}
		}
	}

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
		ReleaseTextureResources();
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

		if (m_sceneAsset)
		{
			m_scene = Alloc<Scene>(TypedRID<EntityResource>(m_sceneAsset), true);
		}
		else
		{
			m_scene = Alloc<Scene>();
		}
	}

	void PreviewWindow::RebuildScene()
	{
		RecreateScene();
		PreviewGenerator::SetupDefaultEnvironment(m_scene);
		m_scene->ExecuteEvents(false);
		m_framePending = true;
	}

	void PreviewWindow::SetPreview(PreviewGenerator& generator)
	{
		m_mode = PreviewMode::Scene;
		m_sceneAsset = {};
		RecreateScene();
		generator.PopulateScene(m_scene);
		m_scene->ExecuteEvents(false);
		m_framePending = true;
	}

	void PreviewWindow::SetAsset(RID asset)
	{
		m_mode = PreviewMode::Scene;
		m_sceneAsset = asset;
		RebuildScene();
		m_focusRequested = true;
	}

	void PreviewWindow::SetTexture(RID texture)
	{
		m_mode = PreviewMode::Texture;
		m_textureRID = texture;
		m_textureCache = RenderResourceCache::GetTextureCache(texture, false);
		m_texture = m_textureCache ? m_textureCache->texture : nullptr;
		m_textureVersion = Resources::GetVersion(texture);
		m_mipLevel = 0;
		m_textureZoom = 1.0f;
		m_texturePan = {0, 0};
		m_fitRequested = true;
		m_textureResourcesDirty = true;
		m_focusRequested = true;
	}

	void PreviewWindow::Clear()
	{
		m_mode = PreviewMode::Scene;
		m_sceneAsset = {};
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
		if (m_mode != PreviewMode::Scene || m_context == nullptr || m_scene == nullptr)
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
		const bool textureMode = m_mode == PreviewMode::Texture;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, textureMode ? ImGui::GetStyle().WindowPadding : ImVec2(0, 0));

		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar);

		if (m_focusRequested)
		{
			ImGui::SetWindowFocus();
			m_focusRequested = false;
		}

		if (textureMode)
		{
			DrawTexture();
		}
		else
		{
			DrawScene();
		}

		ImGui::End();
	}

	void PreviewWindow::DrawScene()
	{
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
	}

	void PreviewWindow::ReleaseTextureResources()
	{
		if (m_textureView)
		{
			m_textureView->Destroy();
			m_textureView = nullptr;
		}
		if (m_textureSampler)
		{
			m_textureSampler->Destroy();
			m_textureSampler = nullptr;
		}
	}

	void PreviewWindow::RebuildTextureResources()
	{
		ReleaseTextureResources();
		m_textureResourcesDirty = false;

		if (!m_texture) return;

		const TextureDesc& desc = m_texture->GetDesc();
		if (m_mipLevel >= desc.mipLevels)
		{
			m_mipLevel = desc.mipLevels - 1;
		}

		FilterMode  filterMode = FilterMode::Linear;
		AddressMode addressMode = AddressMode::Repeat;
		if (ResourceObject textureObject = Resources::Read(m_textureRID))
		{
			filterMode = textureObject.GetEnum<FilterMode>(TextureResource::FilterMode);
			addressMode = textureObject.GetEnum<AddressMode>(TextureResource::WrapMode);
		}

		SamplerDesc samplerDesc{};
		samplerDesc.minFilter = filterMode;
		samplerDesc.magFilter = filterMode;
		samplerDesc.mipmapFilter = filterMode;
		samplerDesc.addressModeU = addressMode;
		samplerDesc.addressModeV = addressMode;
		samplerDesc.addressModeW = addressMode;
		samplerDesc.debugName = "PreviewWindow";
		m_textureSampler = Graphics::CreateSampler(samplerDesc);

		TextureViewDesc viewDesc{};
		viewDesc.texture = m_texture;
		viewDesc.type = TextureViewType::Type2D;
		viewDesc.baseMipLevel = m_mipLevel;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.debugName = "PreviewWindow";
		m_textureView = Graphics::CreateTextureView(viewDesc);
	}

	void PreviewWindow::DrawTexture()
	{
		GPUTexture* current = m_textureCache ? m_textureCache->texture : nullptr;
		if (current != m_texture)
		{
			m_texture = current;
			m_textureResourcesDirty = true;
		}

		if (u64 version = Resources::GetVersion(m_textureRID); version != m_textureVersion)
		{
			m_textureVersion = version;
			m_textureResourcesDirty = true;
		}

		if (m_texture && m_textureResourcesDirty)
		{
			RebuildTextureResources();
		}

		if (!m_texture || !m_textureView)
		{
			ImGuiCentralizedText(m_textureCache ? "Loading texture..." : "No texture to display");
			return;
		}

		DrawTextureToolbar();

		const f32 scale = ImGui::GetStyle().ScaleFactor;
		const f32 spacing = ImGui::GetStyle().ItemSpacing.y;
		const f32 infoHeight = ImGui::GetFrameHeight() + 8.0f * scale;

		ImVec2 region = ImGui::GetContentRegionAvail();
		f32    viewportHeight = Math::Max(region.y - infoHeight - spacing, 1.0f);

		ImGui::BeginChild("##texture-viewport", ImVec2(0, viewportHeight), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawTextureViewport();
		ImGui::EndChild();

		DrawTextureInfo();
	}

	void PreviewWindow::DrawTextureToolbar()
	{
		const TextureDesc& desc = m_texture->GetDesc();
		const f32          scale = ImGui::GetStyle().ScaleFactor;

		ScopedStyleVar pad(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * scale, 4.0f * scale));
		ImGui::BeginChild("##texture-toolbar", ImVec2(0, ImGui::GetFrameHeight() + 8.0f * scale), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

		if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS_MINUS))
		{
			m_textureZoom = Math::Clamp(m_textureZoom / 1.25f, 0.02f, 64.0f);
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS_PLUS))
		{
			m_textureZoom = Math::Clamp(m_textureZoom * 1.25f, 0.02f, 64.0f);
		}

		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%d%%", static_cast<i32>(m_textureZoom * 100.0f + 0.5f));

		ImGui::SameLine();
		if (ImGui::Button("Fit"))
		{
			m_fitRequested = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("1:1"))
		{
			m_textureZoom = 1.0f;
			m_texturePan = Vec2{0, 0};
		}

		if (desc.mipLevels > 1)
		{
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Mip");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f * scale);
			i32 mip = static_cast<i32>(m_mipLevel);
			if (ImGui::SliderInt("##miplevel", &mip, 0, static_cast<i32>(desc.mipLevels) - 1))
			{
				m_mipLevel = static_cast<u32>(mip);
				m_textureResourcesDirty = true;
			}
		}

		ImGui::EndChild();
	}

	void PreviewWindow::DrawTextureViewport()
	{
		const TextureDesc& desc = m_texture->GetDesc();

		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 origin = ImGui::GetCursorScreenPos();

		const f32 texW = static_cast<f32>(desc.extent.width);
		const f32 texH = static_cast<f32>(desc.extent.height);

		if (m_fitRequested)
		{
			m_fitRequested = false;
			m_texturePan = Vec2{0, 0};
			if (texW > 0.0f && texH > 0.0f && avail.x > 0.0f && avail.y > 0.0f)
			{
				m_textureZoom = Math::Clamp(Math::Min(avail.x / texW, avail.y / texH), 0.02f, 64.0f);
			}
		}

		const f32 dispW = texW * m_textureZoom;
		const f32 dispH = texH * m_textureZoom;

		ImVec2 imgMin = ImVec2(
			origin.x + (avail.x - dispW) * 0.5f + m_texturePan.x,
			origin.y + (avail.y - dispH) * 0.5f + m_texturePan.y);
		ImVec2 imgMax = ImVec2(imgMin.x + dispW, imgMin.y + dispH);
		ImVec2 clipMax = ImVec2(origin.x + avail.x, origin.y + avail.y);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin, clipMax, IM_COL32(25, 25, 25, 255));
		DrawChecker(drawList, imgMin, imgMax, origin, clipMax);
		drawList->AddImage(GetImGuiTextureId(m_textureView, m_textureSampler), imgMin, imgMax);

		ImGui::InvisibleButton("##texture-canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsItemActive())
		{
			m_texturePan.x += io.MouseDelta.x;
			m_texturePan.y += io.MouseDelta.y;
		}

		if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
		{
			const f32 fx = dispW > 0.0f ? (io.MousePos.x - imgMin.x) / dispW : 0.5f;
			const f32 fy = dispH > 0.0f ? (io.MousePos.y - imgMin.y) / dispH : 0.5f;

			m_textureZoom = Math::Clamp(m_textureZoom * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f), 0.02f, 64.0f);

			const f32 newW = texW * m_textureZoom;
			const f32 newH = texH * m_textureZoom;

			m_texturePan.x = io.MousePos.x - fx * newW - origin.x - (avail.x - newW) * 0.5f;
			m_texturePan.y = io.MousePos.y - fy * newH - origin.y - (avail.y - newH) * 0.5f;
		}
	}

	void PreviewWindow::DrawTextureInfo()
	{
		const TextureDesc& desc = m_texture->GetDesc();
		const f32          scale = ImGui::GetStyle().ScaleFactor;

		ScopedStyleVar pad(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 4.0f * scale));
		ImGui::BeginChild("##texture-info", ImVec2(0, ImGui::GetFrameHeight() + 8.0f * scale), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

		const char* formatName = "Unknown";
		if (ReflectType* formatType = Reflection::FindType<TextureFormat>())
		{
			if (ReflectValue* formatValue = formatType->FindValueByCode(static_cast<i64>(desc.format)))
			{
				formatName = formatValue->GetDesc().CStr();
			}
		}

		const f32 gap = 16.0f * scale;

		ImGui::AlignTextToFramePadding();
		ImGui::Text("%u x %u", desc.extent.width, desc.extent.height);
		ImGui::SameLine(0, gap);
		ImGui::TextUnformatted(formatName);
		ImGui::SameLine(0, gap);
		ImGui::Text("Mips: %u", desc.mipLevels);
		ImGui::SameLine(0, gap);
		ImGui::Text("Layers: %u", desc.arrayLayers);
		ImGui::SameLine(0, gap);
		ImGui::Text("Viewing Mip %u  (%u x %u)", m_mipLevel, MipSize(desc.extent.width, m_mipLevel), MipSize(desc.extent.height, m_mipLevel));

		ImGui::EndChild();
	}

	void PreviewWindow::DoRefresh()
	{
		if (m_mode == PreviewMode::Texture)
		{
			ReleaseTextureResources();
			m_textureCache.reset();
			m_textureCache = RenderResourceCache::GetTextureCache(m_textureRID, false);
			m_texture = m_textureCache ? m_textureCache->texture : nullptr;
			m_textureVersion = Resources::GetVersion(m_textureRID);
			m_textureResourcesDirty = true;
		}
		else
		{
			if (m_context)
			{
				m_context->Destroy();
				m_context = nullptr;
			}

			if (m_sceneAsset)
			{
				RebuildScene();
			}
			else
			{
				m_framePending = true;
			}
		}
	}

	PreviewWindow* PreviewWindow::FindPreview(EditorWorkspace* workspace)
	{
		struct FindContext
		{
			PreviewWindow* found = nullptr;
		};

		FindContext ctx{};

		workspace->IterateWindows([](EditorWindow* window, VoidPtr userData)
		{
			FindContext* context = static_cast<FindContext*>(userData);
			if (context->found) return;
			if (PreviewWindow* preview = window->SafeCast<PreviewWindow>())
			{
				context->found = preview;
			}
		}, &ctx);

		return ctx.found;
	}

	PreviewWindow* PreviewWindow::GetOrCreate(EditorWorkspace* workspace)
	{
		if (PreviewWindow* preview = FindPreview(workspace))
		{
			return preview;
		}

		workspace->OpenWindow<PreviewWindow>();
		return FindPreview(workspace);
	}

	void PreviewWindow::Refresh()
	{
		EditorWorkspace* workspace = Editor::GetActiveWorkspace();
		if (!workspace) return;

		workspace->IterateWindows([](EditorWindow* window, VoidPtr)
		{
			if (PreviewWindow* preview = window->SafeCast<PreviewWindow>())
			{
				preview->DoRefresh();
			}
		});
	}

	void PreviewWindow::OpenAsset(RID asset)
	{
		EditorWorkspace* workspace = Editor::GetActiveWorkspace();
		if (!workspace) return;

		if (PreviewWindow* preview = GetOrCreate(workspace))
		{
			preview->SetAsset(asset);
		}
	}

	void PreviewWindow::OpenTexture(RID texture)
	{
		EditorWorkspace* workspace = Editor::GetActiveWorkspace();
		if (!workspace) return;

		if (PreviewWindow* preview = GetOrCreate(workspace))
		{
			preview->SetTexture(texture);
		}
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
