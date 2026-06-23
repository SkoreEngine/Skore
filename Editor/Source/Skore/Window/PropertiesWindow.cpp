#include "Skore/Window/PropertiesWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Selection.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/EntityTracker.hpp"
#include "Skore/Scene/LayerSystem.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"
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
			resources.EmplaceBack(RenderPipelineResource{.name = OutputColorName, .type = RenderPipelineResourceType::Attachment, .format = Format::RGBA8_UNORM, .samples = 1});
			return resources;
		}

		RenderPipelineModuleSetup GetSetup() override
		{
			return {};
		}
	};

	PropertiesWindow::PropertiesWindow()
	{
		Event::Bind<OnEntitySelection, &PropertiesWindow::EntitySelection>(this);
		Event::Bind<OnEntityDeselection, &PropertiesWindow::EntityDeselection>(this);

		Event::Bind<OnEntityDebugSelection, &PropertiesWindow::EntityDebugSelection>(this);
		Event::Bind<OnEntityDebugDeselection, &PropertiesWindow::EntityDebugDeselection>(this);

		Event::Bind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
		Event::Bind<OnResourceSelection, &PropertiesWindow::ResourceSelection>(this);
	}

	PropertiesWindow::~PropertiesWindow()
	{
		Event::Unbind<OnEntitySelection, &PropertiesWindow::EntitySelection>(this);
		Event::Unbind<OnEntityDeselection, &PropertiesWindow::EntityDeselection>(this);

		Event::Unbind<OnEntityDebugSelection, &PropertiesWindow::EntityDebugSelection>(this);
		Event::Unbind<OnEntityDebugDeselection, &PropertiesWindow::EntityDebugDeselection>(this);

		Event::Unbind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
		Event::Unbind<OnResourceSelection, &PropertiesWindow::ResourceSelection>(this);

		ClearImportSettingsDraft();
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
		DestroyPreviewGenerator();
	}

	void PropertiesWindow::Init(VoidPtr userData)
	{
		RecreateScene();
		PreviewGenerator::SetupDefaultEnvironment(m_scene);
		m_scene->ExecuteEvents(false);
		m_framePending = true;
		m_previewHeight = 250 * ImGui::GetStyle().ScaleFactor;
	}

	void PropertiesWindow::Render(GPUCommandBuffer* cmd)
	{
		if (!m_previewVisible || m_mode != PreviewMode::Scene || m_context == nullptr || m_scene == nullptr)
		{
			return;
		}

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

	void PropertiesWindow::DrawEntity(u32 id, SceneEditor* sceneEditor, RID entity)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		if (!entity) return;

		if (Resources::GetType(entity)->GetID() != sktypeid(EntityResource)) return;

		ResourceObject entityObject = Resources::Read(entity);

		bool readOnly = sceneEditor->IsReadOnly();

		ImGuiInputTextFlags nameFlags = 0;
		if (readOnly)
		{
			nameFlags |= ImGuiInputTextFlags_ReadOnly;
		}

		if (ImGui::BeginTable("#entity-table", 2))
		{
			ImGui::BeginDisabled(readOnly);

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);


			if (Editor::DebugOptionsEnabled())
			{
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();

				ImGui::Text("%s", "RID");
				ImGui::TableNextColumn();

				ImGui::SetNextItemWidth(-1);
				ImGuiInputTextReadOnly(id + 5, ToString(entity.id));
			}


			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();

			ImGui::Text("Name");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = entityObject.GetString(EntityResource::Name);

			u32 hash = HashValue(reinterpret_cast<usize>(&entity));

			if (ImGuiInputText(hash, stringCache, nameFlags))
			{
				renamingCache = stringCache;
				renamingFocus = true;
				renamingEntity = entity;
			}

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				sceneEditor->Rename(renamingEntity, renamingCache);
				renamingEntity = {};
				renamingFocus = false;
				renamingCache.Clear();
			}

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = entityObject.GetUUID().ToString();

			ImGuiInputText(hash + 10, stringCache, ImGuiInputTextFlags_ReadOnly);

			// Layer dropdown
			{
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Layer");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);

				u8 currentLayer = static_cast<u8>(entityObject.GetUInt(EntityResource::Layer));
				StringView currentLayerName = LayerSystem::GetLayerName(currentLayer);
				if (currentLayerName.Empty())
				{
					stringCache = String("Layer ") + ToString(static_cast<u64>(currentLayer));
				}
				else
				{
					stringCache = currentLayerName;
				}

				if (ImGui::BeginCombo("###layer_combo", stringCache.CStr()))
				{
					for (u8 i = 0; i < MaxLayers; ++i)
					{
						StringView layerName = LayerSystem::GetLayerName(i);
						if (layerName.Empty() && i != 0) continue;

						String label = layerName.Empty() ? String("Layer ") + ToString(static_cast<u64>(i)) : String(layerName);
						bool isSelected = (i == currentLayer);
						if (ImGui::Selectable(label.CStr(), isSelected))
						{
							UndoRedoScope* scope = Editor::CreateUndoRedoScope("Change Layer");
							ResourceObject w = Resources::Write(entity);
							w.SetUInt(EntityResource::Layer, i);
							w.Commit(scope);
						}
						if (isSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}

			// if (ReflectType* reflectEntityType = Reflection::FindType<TransformType>())
			// {
			// 	TransformType entityType = entityObject.GetEnum<TransformType>(EntityResource::Type);
			//
			// 	ImGui::TableNextColumn();
			// 	ImGui::AlignTextToFramePadding();
			// 	ImGui::Text("Type");
			// 	ImGui::TableNextColumn();
			// 	ImGui::SetNextItemWidth(-1);
			//
			// 	ReflectValue* selectedValue = reflectEntityType->FindValueByCode(static_cast<i64>(entityType));
			//
			// 	if (ImGui::BeginCombo("###", selectedValue ? selectedValue->GetDesc().CStr() : ""))
			// 	{
			// 		for (ReflectValue* value : reflectEntityType->GetValues())
			// 		{
			// 			if (ImGui::Selectable(value->GetDesc().CStr()))
			// 			{
			// 				sceneEditor->ChangeTransformType(entity, static_cast<TransformType>(value->GetCode()));
			// 			}
			// 		}
			// 		ImGui::EndCombo();
			// 	}
			// }

			ImGui::EndDisabled();
			ImGui::EndTable();
		}

		ImGui::Dummy(ImVec2(0, 10 * style.ScaleFactor));

		f32  width = ImGui::GetContentRegionAvail().x;
		auto size = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;

		ImGui::BeginHorizontal("horizontal-01", ImVec2(width, size));

		ImGui::Spring(1.f);
		bool addComponent = false;

		ImGui::BeginDisabled(readOnly);

		if (ImGuiBorderedButton("Add Component", ImVec2(width * 2 / 3, size)))
		{
			addComponent = true;
		}

		ImGui::EndDisabled();

		ImVec2 max = ImGui::GetItemRectMax();
		ImVec2 min = ImGui::GetItemRectMin();

		ImGui::Spring(1.f);

		ImGui::EndHorizontal();

		if (entityObject.GetPrototype())
		{
			ImGui::BeginHorizontal(9999, ImVec2(width, size));
			ImGui::Spring(1.f);

			if (ImGuiBorderedButton("Open Prototype", ImVec2((width * 2) / 3, size)))
			{
				RID prototype = entityObject.GetPrototype();
				EditorWorkspace* ws = workspace;
				Editor::ExecuteOnMainThread([ws, prototype]()
				{
					ws->GetSceneEditor()->OpenEntity(prototype);
				});
			}

			ImGui::Spring(1.f);
			ImGui::EndHorizontal();
		}

		ImGui::Dummy(ImVec2(0, 5 * style.ScaleFactor));

		bool openComponentSettings = false;

		auto drawCollapsingHeader = [&](RID rid, StringView formattedName, StringView scopeName, u32 index)
		{
			bool fromPrototype = Resources::GetParent(rid) != entity;

			bool propClicked = false;
			bool open = ImGuiCollapsingHeaderProps(HashValue(rid.id), formattedName.CStr(), &propClicked);
			if (propClicked)
			{
				openComponentSettings = true;
				selectedComponent = rid;
				selectedComponentIndex = index;
			}

			if (open)
			{
				ImGui::BeginDisabled(readOnly || fromPrototype);
				ImGui::Indent();

				ImGuiDrawResource(ImGuiDrawResourceInfo{
					.rid = rid,
					.scopeName = scopeName,
				});

				ImGui::Unindent();
				ImGui::EndDisabled();
			}
		};

		u32 componentCount = 0;
		entityObject.IterateSubObjectList(EntityResource::Components, [&](RID component)
		{
			if (ResourceType* componentType = Resources::GetType(component);
				componentType != nullptr &&
				componentType->GetReflectType() != nullptr)
			{
				String formattedName = FormatName(componentType->GetReflectType()->GetSimpleName());
				String scope = String(formattedName).Append(" Update");
				drawCollapsingHeader(component, formattedName, scope, componentCount);
			}
			componentCount++;
		});

		if (addComponent)
		{
			ImGui::OpenPopup("add-component-popup");
		}

		ImGui::SetNextWindowPos(ImVec2(min.x, max.y + 5));
		auto sizePopup = max.x - min.x;
		ImGui::SetNextWindowSize(ImVec2(sizePopup, 0), ImGuiCond_Appearing);

		auto popupRes = ImGuiBeginPopupMenu("add-component-popup", 0, false);
		if (popupRes)
		{
			ImGui::SetNextItemWidth(sizePopup - style.WindowPadding.x * 2);
			ImGuiSearchInputText(id + 100, searchComponentString);
			ImGui::Separator();

			//TODO - cache it
			HashMap<String, Array<ReflectType*>> categories;
			Array<ReflectType*> components;

			for (TypeID componentId : Reflection::GetDerivedTypes(TypeInfo<Component>::ID()))
			{
				if (ReflectType* refletionType = Reflection::FindTypeById(componentId))
				{
					if (refletionType->GetDefaultConstructor() == nullptr) continue;

					if (const ComponentDesc* componentDesc = refletionType->GetAttribute<ComponentDesc>())
					{
						if (!componentDesc->category.Empty())
						{
							auto it = categories.Find(componentDesc->category);
							if (it == categories.end())
							{
								it = categories.Insert(componentDesc->category, Array<ReflectType*>()).first;
							}
							it->second.EmplaceBack(refletionType);
							continue;
						}
					}
					components.EmplaceBack(refletionType);
				}
			}

			auto drawComponent = [&](ReflectType* refletionType)
			{
				String name = FormatName(refletionType->GetSimpleName());
				if (ImGui::Selectable(name.CStr()))
				{
					sceneEditor->AddComponent(entity, refletionType->GetProps().typeId);
				}
			};

			for (auto it : categories)
			{
				if (ImGui::BeginMenu(it.first.CStr()))
				{
					for (ReflectType* compType : it.second)
					{
						drawComponent(compType);
					}
					ImGui::EndMenu();
				}
			}


			for (ReflectType* compType : components)
			{
				drawComponent(compType);
			}
		}
		ImGuiEndPopupMenu(popupRes);

		if (openComponentSettings)
		{
			ImGui::OpenPopup("open-component-settings");
		}

		bool canRemove = !readOnly;
		bool canMove = !readOnly && selectedComponentIndex < U32_MAX;

		bool popupOpenSettings = ImGuiBeginPopupMenu("open-component-settings", 0, false);
		if (popupOpenSettings && selectedComponent)
		{
			if (ImGui::MenuItem("Reset"))
			{
				sceneEditor->ResetComponent(entity, selectedComponent);
				ImGui::CloseCurrentPopup();
			}

			if (canRemove && ImGui::MenuItem("Remove"))
			{
				sceneEditor->RemoveComponent(entity, selectedComponent);
				ImGui::CloseCurrentPopup();
			}

			if (canMove && selectedComponentIndex > 0 && ImGui::MenuItem("Move Up"))
			{
				sceneEditor->MoveComponentTo(selectedComponent, selectedComponentIndex - 1);
				ImGui::CloseCurrentPopup();
			}

			if (canMove && selectedComponentIndex < (componentCount -1) && ImGui::MenuItem("Move Down"))
			{
				sceneEditor->MoveComponentTo(selectedComponent, selectedComponentIndex + 1);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGuiEndPopupMenu(popupOpenSettings);
	}

	void PropertiesWindow::DrawDebugEntity(u32 id, SceneEditor* sceneEditor, Entity* entity)
	{
		if (!EntityTracker::IsAlive(entity)) return;

		ImGuiStyle& style = ImGui::GetStyle();

		if (ImGui::BeginTable("#entity-table", 2))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();

			ImGui::Text("Name");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = entity->GetName();
			u32 hash = HashValue(PtrToInt(entity));

			ImGuiInputTextReadOnly(hash, stringCache);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			if (entity->GetRID())
			{
				static String uuid = Resources::GetUUID(entity->GetRID()).ToString();
				ImGuiInputTextReadOnly(hash + 10, uuid);
			}
			else
			{
				static String uuid = UUID().ToString();
				ImGuiInputTextReadOnly(hash + 10, uuid);
			}

			ImGui::EndTable();
		}

		ImGui::Dummy(ImVec2(0, 5 * style.ScaleFactor));

		for (Component* component: entity->GetComponents())
		{
			ImGui::PushID(component);
			ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
			if (ImGui::CollapsingHeader(FormatName(component->GetType()->GetSimpleName()).CStr(), ImGuiTreeNodeFlags_AllowOverlap))
			{
				ImGui::Indent();

				ImGuiDrawObject(ImGuiDrawObjectInfo{
					.object = component,
					.userData = this,
				});

				ImGui::Unindent();
			}
			ImGui::PopID();
		}

	}

	void PropertiesWindow::EntityDebugSelection(u32 workspaceId, Entity* entity)
	{
		if (workspace->GetId() != workspaceId) return;

		if (!entity && !selectedDebugEntity) return;

		ClearSelection();

		selectedDebugEntity = entity;
	}

	void PropertiesWindow::EntityDebugDeselection(u32 workspaceId, Entity* entity)
	{
		if (workspace->GetId() != workspaceId) return;

		if (!entity && !selectedDebugEntity) return;
		if (selectedDebugEntity == entity)
		{
			ClearSelection();
		}
	}

	void PropertiesWindow::DrawAsset(u32 id, RID asset)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		if (ImGui::BeginTable("#asset-table", 2))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();

			ImGui::Text("Name");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = ResourceAssets::GetAssetName(asset);
			u32 hash = HashValue(asset);

			if (ImGuiInputText(hash, stringCache))
			{
				renamingCache = stringCache;
				renamingFocus = true;
			}

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				if (!renamingCache.Empty())
				{
					UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Rename Finished");
					ResourceObject write = Resources::Write(asset);
					write.SetString(ResourceAsset::Name, renamingCache);
					write.Commit(scope);
				}

				renamingFocus = false;
				renamingCache.Clear();
			}

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = ResourceAssets::GetAssetUUID(asset).ToString();
			ImGuiInputTextReadOnly(hash + 10, stringCache);
			ImGui::EndTable();
		}

		ImGui::Dummy(ImVec2(0, 5 * style.ScaleFactor));


		if (RID importSettings = ResourceAssets::GetImportSettings(asset))
		{
			if (importSettingsPendingApply && importSettings == importSettingsPendingApply)
			{
				importSettingsSource = {};
				importSettingsDraft = {};
				importSettingsPendingApply = {};
				importSettingsVersion = U64_MAX;
				importSettingsSourceVersion = U64_MAX;
			}

			RID importSettingsEdit = GetImportSettingsDraft(importSettings);
			if (!importSettingsEdit) return;

			f32  width = ImGui::GetContentRegionAvail().x;
			auto size = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;

			ImGui::BeginHorizontal("horizontal-01", ImVec2(width, size));

			ImGui::Spring(1.f);

			u64 currVersion = Resources::GetVersion(importSettingsEdit);
			bool applying = importSettingsPendingApply != RID{};

			ImGui::BeginDisabled(applying || currVersion == importSettingsVersion);

			if (ImGuiBorderedButton("Apply", ImVec2(width * 1 / 3, size)))
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Apply Import Changes");
				importSettingsVersion = currVersion;
				importSettingsPendingApply = importSettingsEdit;
				ResourceAssets::CookAsset(asset, importSettingsEdit, scope);
				RefreshPreview();
			}

			ImGui::EndDisabled();

			ImGui::BeginDisabled(applying);

			if (ImGuiBorderedButton("Reimport", ImVec2(width * 1 / 3, size)))
			{
				ResourceAssets::ReimportAssetFromFile(asset);
			}

			ImGui::EndDisabled();

			ImGui::Spring(1.f);

			ImGui::EndHorizontal();


			ImGui::BeginDisabled(applying);
			ImGuiDrawResource(ImGuiDrawResourceInfo{
				.rid = importSettingsEdit,
				.scopeName = "Import Settings Edit",
				.drawCollapseHeader = true
			});
			ImGui::EndDisabled();
		}
		else
		{
			ResourceType* type = Resources::GetType(asset);
			if (ImGui::CollapsingHeader(FormatName(type->GetSimpleName()).CStr()), ImGuiTreeNodeFlags_DefaultOpen)
			{
				ImGui::Indent();
				ImGuiDrawResource(ImGuiDrawResourceInfo{
					.rid = asset,
					.scopeName = "Asset Edit",
					.ignoreFirstItem = true
				});
				ImGui::Unindent();
			}
		}
	}

	const char* PropertiesWindow::GetTitle() const
	{
		return ICON_FA_CIRCLE_INFO " Properties";
	}

	void PropertiesWindow::Draw(bool& open)
	{
		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar);

		if (m_focusRequested)
		{
			ImGui::SetWindowFocus();
			m_focusRequested = false;
		}

		DrawPreview();

		{
			ScopedStyleVar contentPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::BeginChild("##properties-content", ImVec2(0, 0));
		}

		//the selection system prunes runtime entities as they are destroyed (e.g. when the
		//simulation stops); drop a stale cached pointer before it is ever dereferenced
		if (selectedDebugEntity && !Selection::IsSelected(selectedDebugEntity))
		{
			selectedDebugEntity = nullptr;
		}

		if (selectedEntity)
		{
			SceneEditor* sceneEditor = workspace->GetSceneEditor();
			DrawEntity(id, sceneEditor, selectedEntity);
		}
		else if (selectedDebugEntity)
		{
			SceneEditor* sceneEditor = workspace->GetSceneEditor();
			DrawDebugEntity(id, sceneEditor, selectedDebugEntity);
		}
		else if (selectedAsset)
		{
			DrawAsset(id, selectedAsset);
		}
		else if (selectedResource)
		{
			DrawResource(id, selectedResource);
		}
		else
		{
			ImGuiCentralizedText("Select something...");
		}

		ImGui::EndChild();

		ImGui::End();
	}

	void PropertiesWindow::DrawPreview()
	{
		if (!m_previewVisible) return;

		ImGuiStyle& style = ImGui::GetStyle();
		const f32   scale = style.ScaleFactor;
		const f32   thickness = 6.0f * scale;
		const f32   minPreview = 80.0f * scale;
		const f32   minProperties = 120.0f * scale;

		f32 avail = ImGui::GetContentRegionAvail().y;
		f32 maxPreview = Math::Max(minPreview, avail - thickness - minProperties);
		m_previewHeight = Math::Clamp(m_previewHeight, minPreview, maxPreview);

		{
			ScopedStyleVar previewPadding(ImGuiStyleVar_WindowPadding, m_mode == PreviewMode::Texture ? style.WindowPadding : ImVec2(0, 0));
			ImGui::BeginChild("##preview-region", ImVec2(0, m_previewHeight), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			if (m_mode == PreviewMode::Texture)
			{
				DrawTexture();
			}
			else
			{
				DrawScene();
			}
			ImGui::EndChild();
		}

		f32 splitterWidth = ImGui::GetContentRegionAvail().x;
		if (splitterWidth <= 0.0f || thickness <= 0.0f) return;

		ImGui::InvisibleButton("##preview-splitter", ImVec2(splitterWidth, thickness));

		bool hovered = ImGui::IsItemHovered();
		bool active = ImGui::IsItemActive();

		if (hovered || active)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}

		if (active)
		{
			m_previewHeight += ImGui::GetIO().MouseDelta.y;
		}

		ImVec2 splitterMin = ImGui::GetItemRectMin();
		ImVec2 splitterMax = ImGui::GetItemRectMax();
		f32    lineY = (splitterMin.y + splitterMax.y) * 0.5f;
		ImU32  color = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(splitterMin.x, lineY), ImVec2(splitterMax.x, lineY), color, Math::Max(1.0f, scale));
	}

	void PropertiesWindow::RecreateScene()
	{
		if (m_scene)
		{
			DestroyAndFree(m_scene);
			m_scene = nullptr;
		}

		m_scene = Alloc<Scene>();
	}

	void PropertiesWindow::RebuildScene()
	{
		RecreateScene();
		if (m_previewGenerator)
		{
			m_previewGenerator->PopulateScene(m_scene);
		}
		else
		{
			PreviewGenerator::SetupDefaultEnvironment(m_scene);
		}
		m_scene->ExecuteEvents(false);
		m_framePending = true;
	}

	void PropertiesWindow::DestroyPreviewGenerator()
	{
		if (m_previewGenerator)
		{
			DestroyAndFree(m_previewGenerator);
			m_previewGenerator = nullptr;
		}
	}

	void PropertiesWindow::SetPreview(RID asset, TypeID previewType)
	{
		m_mode = PreviewMode::Scene;

		DestroyPreviewGenerator();

		if (ReflectType* reflectType = Reflection::FindTypeById(previewType))
		{
			if (Object* object = reflectType->NewObject())
			{
				m_previewGenerator = object->SafeCast<PreviewGenerator>();
				if (m_previewGenerator)
				{
					m_previewGenerator->asset = asset;
				}
				else
				{
					DestroyAndFree(object);
				}
			}
		}

		RebuildScene();
		m_previewVisible = true;
		m_focusRequested = true;
	}

	void PropertiesWindow::SetTexture(RID texture)
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
		m_previewVisible = true;
		m_focusRequested = true;
	}

	void PropertiesWindow::FrameCamera()
	{
		if (!m_scene) return;

		f32 radius = 7.0f;
		m_orbitTarget = {};

		if (AABB aabb = m_scene->GetBounds())
		{
			m_orbitTarget = aabb.GetCenter();
			radius = aabb.GetRadius();
		}

		//keep a floor so degenerate/point bounds don't collapse the zoom limits to zero
		m_sceneRadius = Math::Max(radius, 0.01f);
		m_orbitDistance = m_sceneRadius / tan(Math::Radians(m_fov) * 0.70f);
	}

	void PropertiesWindow::EnsureContext(Extent extent)
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

	void PropertiesWindow::DrawScene()
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

					//double-click re-frames the camera so the user can always recover the default view
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						m_framePending = true;
					}
					else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
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
						//step by the wheel sign only (like the texture viewer); the raw magnitude can spike
						//on high-resolution mice/touchpads and send the first zoom flying.
						const f32 zoomStep = io.MouseWheel > 0.0f ? (1.0f / 1.15f) : 1.15f;
						m_orbitDistance *= zoomStep;

						//clamp relative to the framed scene so the object can't shrink to a dot or clip inside it
						const f32 minDistance = Math::Max(m_sceneRadius * 0.15f, 0.01f);
						const f32 maxDistance = Math::Max(m_sceneRadius * 12.0f, minDistance);
						m_orbitDistance = Math::Clamp(m_orbitDistance, minDistance, maxDistance);
					}
				}
			}
		}
	}

	void PropertiesWindow::ReleaseTextureResources()
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

	void PropertiesWindow::RebuildTextureResources()
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
		samplerDesc.debugName = "PropertiesWindow";
		m_textureSampler = Graphics::CreateSampler(samplerDesc);

		TextureViewDesc viewDesc{};
		viewDesc.texture = m_texture;
		viewDesc.type = TextureViewType::Type2D;
		viewDesc.baseMipLevel = m_mipLevel;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.debugName = "PropertiesWindow";
		m_textureView = Graphics::CreateTextureView(viewDesc);
	}

	void PropertiesWindow::DrawTexture()
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

	void PropertiesWindow::DrawTextureToolbar()
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

	void PropertiesWindow::DrawTextureViewport()
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

	void PropertiesWindow::DrawTextureInfo()
	{
		const TextureDesc& desc = m_texture->GetDesc();
		const f32          scale = ImGui::GetStyle().ScaleFactor;

		ScopedStyleVar pad(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 4.0f * scale));
		ImGui::BeginChild("##texture-info", ImVec2(0, ImGui::GetFrameHeight() + 8.0f * scale), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

		const char* formatName = "Unknown";
		if (ReflectType* formatType = Reflection::FindType<Format>())
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

	void PropertiesWindow::RefreshPreview()
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

			if (m_previewGenerator)
			{
				RebuildScene();
			}
			else
			{
				m_framePending = true;
			}
		}
	}

	void PropertiesWindow::ClearSelection()
	{
		selectedEntity = {};
		selectedComponent = {};
		selectedDebugEntity = nullptr;
		selectedAsset = {};
		selectedResource = {};
		ClearImportSettingsDraft();
		m_previewVisible = false;
		DestroyPreviewGenerator();
	}

	void PropertiesWindow::ClearImportSettingsDraft()
	{
		if (importSettingsDraft && importSettingsDraft != importSettingsSource && importSettingsDraft != importSettingsPendingApply)
		{
			Resources::Destroy(importSettingsDraft);
		}

		importSettingsSource = {};
		importSettingsDraft = {};
		importSettingsPendingApply = {};
		importSettingsVersion = U64_MAX;
		importSettingsSourceVersion = U64_MAX;
	}

	RID PropertiesWindow::GetImportSettingsDraft(RID importSettings)
	{
		u64 sourceVersion = Resources::GetVersion(importSettings);
		bool draftUnchanged = !importSettingsDraft || Resources::GetVersion(importSettingsDraft) == importSettingsVersion;

		if (importSettingsSource != importSettings || !importSettingsDraft || (draftUnchanged && importSettingsSourceVersion != sourceVersion))
		{
			if (importSettingsDraft && importSettingsDraft != importSettingsPendingApply)
			{
				Resources::Destroy(importSettingsDraft);
			}

			importSettingsSource = importSettings;
			importSettingsSourceVersion = sourceVersion;
			importSettingsDraft = Resources::Clone(importSettings, UUID::RandomUUID());
			importSettingsVersion = Resources::GetVersion(importSettingsDraft);
		}

		return importSettingsDraft;
	}

	void PropertiesWindow::OpenProperties(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<PropertiesWindow>();
	}

	void PropertiesWindow::EntitySelection(u32 workspaceId, RID entity)
	{
		if (workspace->GetId() != workspaceId) return;

		if (!entity && !selectedEntity) return;

		ClearSelection();
		selectedEntity = entity;
	}

	void PropertiesWindow::EntityDeselection(u32 workspaceId, RID entity)
	{
		if (workspace->GetId() != workspaceId) return;

		if (!entity && !selectedEntity) return;
		if (selectedEntity == entity)
		{
			ClearSelection();
		}
	}

	void PropertiesWindow::AssetSelection(u32 workspaceId, RID assetId)
	{
		if (workspace->GetId() != workspaceId) return;

		ClearSelection();
		selectedAsset = assetId;

		if (!assetId) return;

		//textures keep their dedicated 2D viewer; everything else previews through its PreviewGenerator.
		if (ResourceType* type = Resources::GetType(assetId); type != nullptr && type->GetID() == sktypeid(TextureResource))
		{
			SetTexture(assetId);
		}
		else if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(assetId))
		{
			if (TypeID previewType = handler->GetPreviewGenerator())
			{
				SetPreview(assetId, previewType);
			}
		}
	}

	void PropertiesWindow::ResourceSelection(u32 workspaceId, RID resourceId)
	{
		if (workspace->GetId() != workspaceId) return;

		ClearSelection();
		selectedResource = resourceId;
	}

	void PropertiesWindow::DrawResource(u32 id, RID resource)
	{
		ResourceType* type = Resources::GetType(resource);
		if (!type) return;

		if (ImGui::CollapsingHeader(FormatName(type->GetSimpleName()).CStr()), ImGuiTreeNodeFlags_DefaultOpen)
		{
			ImGui::Indent();
			ImGuiDrawResource(ImGuiDrawResourceInfo{
				.rid = resource,
				.scopeName = "Resource Edit",
			});
			ImGui::Unindent();
		}
	}

	void PropertiesWindow::RegisterType(NativeReflectType<PropertiesWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Properties", .action = OpenProperties});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightBottom,
			.workspaceTypes = {WorkspaceTypes::All}
		});
	}

	void RegisterPreviewModule()
	{
		Reflection::Type<PreviewOutputModule>();
	}
}
