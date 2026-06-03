#include "Skore/Window/TextureViewWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Resource/Resources.hpp"


namespace Skore
{
	namespace
	{
		template <typename T>
		bool EnumCombo(const char* id, T& value)
		{
			ReflectType* type = Reflection::FindType<T>();
			if (!type) return false;

			ReflectValue* current = type->FindValueByCode(static_cast<i64>(value));

			bool changed = false;
			if (ImGui::BeginCombo(id, current ? current->GetDesc().CStr() : ""))
			{
				for (ReflectValue* reflectValue : type->GetValues())
				{
					bool selected = reflectValue->GetCode() == static_cast<i64>(value);
					if (ImGui::Selectable(reflectValue->GetDesc().CStr(), selected))
					{
						value = static_cast<T>(reflectValue->GetCode());
						changed = true;
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

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

	TextureViewWindow::~TextureViewWindow()
	{
		ReleaseResources();
	}

	void TextureViewWindow::Init(VoidPtr userData)
	{
		textureRID = userData ? *static_cast<RID*>(userData) : RID{};
		textureCache = RenderResourceCache::GetTextureCache(textureRID, false);
		texture = textureCache ? textureCache->texture : nullptr;

		if (ResourceObject textureObject = Resources::Read(textureRID))
		{
			filterMode = textureObject.GetEnum<FilterMode>(TextureResource::FilterMode);
			addressMode = textureObject.GetEnum<AddressMode>(TextureResource::WrapMode);
		}

		resourcesDirty = true;
		fitRequested = true;
	}

	void TextureViewWindow::ReleaseResources()
	{
		if (textureView)
		{
			textureView->Destroy();
			textureView = nullptr;
		}
		if (sampler)
		{
			sampler->Destroy();
			sampler = nullptr;
		}
	}

	void TextureViewWindow::RebuildResources()
	{
		ReleaseResources();
		resourcesDirty = false;

		if (!texture) return;

		const TextureDesc& desc = texture->GetDesc();
		if (mipLevel >= desc.mipLevels)
		{
			mipLevel = desc.mipLevels - 1;
		}

		SamplerDesc samplerDesc{};
		samplerDesc.minFilter = filterMode;
		samplerDesc.magFilter = filterMode;
		samplerDesc.mipmapFilter = filterMode;
		samplerDesc.addressModeU = addressMode;
		samplerDesc.addressModeV = addressMode;
		samplerDesc.addressModeW = addressMode;
		samplerDesc.debugName = "TextureViewWindow";
		sampler = Graphics::CreateSampler(samplerDesc);

		TextureViewDesc viewDesc{};
		viewDesc.texture = texture;
		viewDesc.type = TextureViewType::Type2D;
		viewDesc.baseMipLevel = mipLevel;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.debugName = "TextureViewWindow";
		textureView = Graphics::CreateTextureView(viewDesc);
	}

	void TextureViewWindow::WriteSamplerSettings()
	{
		if (!textureRID) return;

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Update Texture Sampler");
		ResourceObject textureObject = Resources::Write(textureRID);
		textureObject.SetEnum(TextureResource::FilterMode, filterMode);
		textureObject.SetEnum(TextureResource::WrapMode, addressMode);
		textureObject.Commit(scope);
	}

	void TextureViewWindow::Draw(bool& open)
	{
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar);

		GPUTexture* current = textureCache ? textureCache->texture : nullptr;
		if (current != texture)
		{
			texture = current;
			resourcesDirty = true;
		}

		if (texture && resourcesDirty)
		{
			RebuildResources();
		}

		if (!texture || !textureView)
		{
			ImGuiCentralizedText(textureCache ? "Loading texture..." : "No texture to display");
			ImGui::End();
			return;
		}

		const f32 scale = ImGui::GetStyle().ScaleFactor;
		const f32 propWidth = 280.0f * scale;

		DrawToolbar();

		ImVec2 region = ImGui::GetContentRegionAvail();

		ImGui::BeginChild("##texture-viewport", ImVec2(Math::Max(region.x - propWidth, 1.0f), 0), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawViewport();
		ImGui::EndChild();

		ImGui::SameLine();

		{
			ScopedStyleVar pad(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 8.0f * scale));
			ImGui::BeginChild("##texture-properties", ImVec2(propWidth, 0), ImGuiChildFlags_Border);
			DrawProperties();
			ImGui::EndChild();
		}

		ImGui::End();
	}

	void TextureViewWindow::DrawToolbar()
	{
		const TextureDesc& desc = texture->GetDesc();
		const f32          scale = ImGui::GetStyle().ScaleFactor;

		ScopedStyleVar pad(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * scale, 4.0f * scale));
		ImGui::BeginChild("##texture-toolbar", ImVec2(0, ImGui::GetFrameHeight() + 8.0f * scale), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

		if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS_MINUS))
		{
			zoom = Math::Clamp(zoom / 1.25f, 0.02f, 64.0f);
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS_PLUS))
		{
			zoom = Math::Clamp(zoom * 1.25f, 0.02f, 64.0f);
		}

		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%d%%", static_cast<i32>(zoom * 100.0f + 0.5f));

		ImGui::SameLine();
		if (ImGui::Button("Fit"))
		{
			fitRequested = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("1:1"))
		{
			zoom = 1.0f;
			pan = Vec2{0, 0};
		}

		if (desc.mipLevels > 1)
		{
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Mip");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f * scale);
			i32 mip = static_cast<i32>(mipLevel);
			if (ImGui::SliderInt("##miplevel", &mip, 0, static_cast<i32>(desc.mipLevels) - 1))
			{
				mipLevel = static_cast<u32>(mip);
				resourcesDirty = true;
			}
		}

		ImGui::EndChild();
	}

	void TextureViewWindow::DrawViewport()
	{
		const TextureDesc& desc = texture->GetDesc();

		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 origin = ImGui::GetCursorScreenPos();

		const f32 texW = static_cast<f32>(desc.extent.width);
		const f32 texH = static_cast<f32>(desc.extent.height);

		if (fitRequested)
		{
			fitRequested = false;
			pan = Vec2{0, 0};
			if (texW > 0.0f && texH > 0.0f && avail.x > 0.0f && avail.y > 0.0f)
			{
				zoom = Math::Clamp(Math::Min(avail.x / texW, avail.y / texH), 0.02f, 64.0f);
			}
		}

		const f32 dispW = texW * zoom;
		const f32 dispH = texH * zoom;

		ImVec2 imgMin = ImVec2(
			origin.x + (avail.x - dispW) * 0.5f + pan.x,
			origin.y + (avail.y - dispH) * 0.5f + pan.y);
		ImVec2 imgMax = ImVec2(imgMin.x + dispW, imgMin.y + dispH);
		ImVec2 clipMax = ImVec2(origin.x + avail.x, origin.y + avail.y);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin, clipMax, IM_COL32(25, 25, 25, 255));
		DrawChecker(drawList, imgMin, imgMax, origin, clipMax);
		drawList->AddImage(GetImGuiTextureId(textureView, sampler), imgMin, imgMax);

		ImGui::InvisibleButton("##texture-canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsItemActive())
		{
			pan.x += io.MouseDelta.x;
			pan.y += io.MouseDelta.y;
		}

		if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
		{
			const f32 fx = dispW > 0.0f ? (io.MousePos.x - imgMin.x) / dispW : 0.5f;
			const f32 fy = dispH > 0.0f ? (io.MousePos.y - imgMin.y) / dispH : 0.5f;

			zoom = Math::Clamp(zoom * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f), 0.02f, 64.0f);

			const f32 newW = texW * zoom;
			const f32 newH = texH * zoom;

			pan.x = io.MousePos.x - fx * newW - origin.x - (avail.x - newW) * 0.5f;
			pan.y = io.MousePos.y - fy * newH - origin.y - (avail.y - newH) * 0.5f;
		}
	}

	void TextureViewWindow::DrawProperties()
	{
		const TextureDesc& desc = texture->GetDesc();
		const f32          scale = ImGui::GetStyle().ScaleFactor;
		const f32          labelWidth = 110.0f * scale;

		auto infoLabel = [&](const char* label)
		{
			ImGui::TextDisabled("%s", label);
			ImGui::SameLine(labelWidth);
		};

		ImGui::TextUnformatted("Texture");
		ImGui::Separator();
		ImGui::Spacing();

		infoLabel("Dimensions");
		ImGui::Text("%u x %u", desc.extent.width, desc.extent.height);

		infoLabel("Mip Levels");
		ImGui::Text("%u", desc.mipLevels);

		infoLabel("Array Layers");
		ImGui::Text("%u", desc.arrayLayers);

		const char* formatName = "Unknown";
		if (ReflectType* formatType = Reflection::FindType<TextureFormat>())
		{
			if (ReflectValue* formatValue = formatType->FindValueByCode(static_cast<i64>(desc.format)))
			{
				formatName = formatValue->GetDesc().CStr();
			}
		}
		infoLabel("Format");
		ImGui::TextUnformatted(formatName);

		infoLabel("Viewing Mip");
		ImGui::Text("%u  (%u x %u)", mipLevel, MipSize(desc.extent.width, mipLevel), MipSize(desc.extent.height, mipLevel));

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::TextUnformatted("Sampler");
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextDisabled("Filter Mode");
		ImGui::SameLine(labelWidth);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (EnumCombo("##filtermode", filterMode))
		{
			resourcesDirty = true;
			WriteSamplerSettings();
		}

		ImGui::TextDisabled("Wrap Mode");
		ImGui::SameLine(labelWidth);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (EnumCombo("##wrapmode", addressMode))
		{
			resourcesDirty = true;
			WriteSamplerSettings();
		}
	}

	void TextureViewWindow::Open(RID texture)
	{
		Editor::GetActiveWorkspace()->OpenWindow<TextureViewWindow>(&texture);
	}
}
