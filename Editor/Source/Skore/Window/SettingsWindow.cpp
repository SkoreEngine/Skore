#include "Skore/Window/SettingsWindow.hpp"

#include <algorithm>

#include "imgui.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Scene/LayerSystem.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	const char* SettingsWindow::GetTitle() const
	{
		return title.CStr();
	}

	void SettingsWindow::Init(VoidPtr userData)
	{
		settingsType = TypeID{PtrToInt(userData)};

		if (ReflectType* typeHandler = Reflection::FindTypeById(settingsType))
		{
			title = FormatName(typeHandler->GetSimpleName());
		}

		HashMap<String, std::shared_ptr<SettingsItem>> itemsByPath;

		for (TypeID typeId : Resources::FindTypesByAttribute<EditableSettings>())
		{
			ResourceType* resourceType = Resources::FindTypeByID(typeId);
			if (const EditableSettings* editableSettings = resourceType->GetAttribute<EditableSettings>())
			{
				if (editableSettings->type == settingsType)
				{
					Array<String> items = {};
					Split(StringView{editableSettings->path}, StringView{"/"}, [&](const StringView& item)
					{
						items.EmplaceBack(item);
					});

					if (items.Empty())
					{
						items.EmplaceBack(editableSettings->path);
					}

					String path = "";
					std::shared_ptr<SettingsItem> lastItem = nullptr;

					for (int i = 0; i < items.Size(); ++i)
					{
						const String& itemLabel = items[i];
						path += "/" + itemLabel;

						auto itByPath = itemsByPath.Find(path);
						if (itByPath == itemsByPath.end())
						{
							std::shared_ptr<SettingsItem> newItem = std::make_shared<SettingsItem>();
							newItem->label = itemLabel;
							itByPath = itemsByPath.Insert(path, newItem).first;

							if (lastItem != nullptr)
							{
								lastItem->children.EmplaceBack(newItem);
							}
							else
							{
								rootItems.EmplaceBack(newItem);
							}
						}
						lastItem = itByPath->second;
					}

					if (lastItem != nullptr)
					{
						SettingsEntry entry;
						entry.rid   = Settings::Get(settingsType, typeId);
						entry.type  = resourceType;
						entry.order = editableSettings->order;

						if (lastItem->entries.Empty() || entry.order < lastItem->order)
						{
							lastItem->order = entry.order;
						}
						lastItem->entries.EmplaceBack(entry);
					}
				}
			}
		}

		std::function<void(Array<std::shared_ptr<SettingsItem>>&)> sortItems;
		sortItems = [&](Array<std::shared_ptr<SettingsItem>>& arr)
		{
			std::sort(arr.begin(), arr.end(), [](const std::shared_ptr<SettingsItem>& a, const std::shared_ptr<SettingsItem>& b)
			{
				return a->order < b->order;
			});
			for (auto& item : arr)
			{
				if (!item->children.Empty())
				{
					sortItems(item->children);
				}
			}
		};

		if (!rootItems.Empty())
		{
			sortItems(rootItems);
		}
	}

	void SettingsWindow::Draw(bool& open)
	{
		auto&  style = ImGui::GetStyle();
		ImVec2 padding = style.WindowPadding;

		ScopedStyleVar   windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ScopedStyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));

		ImGuiCenterWindow(ImGuiCond_Appearing);

		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);

		if (ImGui::BeginTable("settings-windows-table", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 300 * style.ScaleFactor);
			ImGui::TableNextColumn();

			ScopedStyleVar childPadding(ImGuiStyleVar_WindowPadding, padding);
			DrawTree();
			ImGui::TableNextColumn();
			DrawSelected();

			ImGui::EndTable();
		}

		ImGui::End();
	}

	void SettingsWindow::Open(TypeID group)
	{
		Editor::GetActiveWorkspace()->OpenWindow<SettingsWindow>(IntToPtr(group));
	}

	void SettingsWindow::DrawTree()
	{
		//left panel
		ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
		ImGui::BeginChild(4000, ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);

		ImGui::SetNextItemWidth(-1);
		ImGuiSearchInputText(4001, searchText);
		ImGui::Dummy(ImVec2(0.0f, 5 * ImGui::GetStyle().ScaleFactor));

		ImGuiBeginTreeNodeStyle();

		for (auto& item : rootItems)
		{
			DrawItem(*item, 0);
		}

		ImGuiEndTreeNodeStyle();

		ImGui::EndChild();
	}

	void SettingsWindow::DrawItem(const SettingsItem& settingsItem, u32 level)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;

		if (selectedItem == &settingsItem)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		bool open = false;

		if (!settingsItem.children.Empty())
		{
			ImGui::SetNextItemOpen(level == 0, ImGuiCond_Once);
			open = ImGuiTreeNode(&settingsItem, settingsItem.label.CStr(), flags);
		}
		else
		{
			ImGuiTreeLeaf(&settingsItem, settingsItem.label.CStr(), flags);
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			selectedItem = &settingsItem;
		}

		if (open)
		{
			for (auto& item : settingsItem.children)
			{
				DrawItem(*item, level + 1);
			}

			ImGui::TreePop();
		}
	}

	void SettingsWindow::DrawSelected() const
	{
		ImGui::BeginChild(5000, ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);

		if (selectedItem && !selectedItem->entries.Empty())
		{
			for (const SettingsEntry& entry : selectedItem->entries)
			{
				ImGui::PushID(IntToPtr(entry.rid.id));

				StringView simpleName = entry.type ? entry.type->GetSimpleName() : StringView{"Settings"};
				constexpr StringView suffix{"Settings"};
				if (simpleName.Size() > suffix.Size() && simpleName.Substr(simpleName.Size() - suffix.Size()) == suffix)
				{
					simpleName = simpleName.Substr(0, simpleName.Size() - suffix.Size());
				}
				String header = FormatName(simpleName);
				if (ImGui::CollapsingHeader(header.CStr(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGuiDrawResource(ImGuiDrawResourceInfo{
						.rid = entry.rid,
						.scopeName = "Settings Edit",
					});

					if (entry.type && entry.type->GetID() == TypeInfo<PhysicsSettings>::ID())
					{
						DrawCollisionMatrix(entry.rid);
					}
				}

				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	void SettingsWindow::DrawCollisionMatrix(RID rid) const
	{
		// Collect active layers (those with names)
		struct LayerInfo { u8 index; StringView name; };
		Array<LayerInfo> activeLayers;
		for (u32 i = 0; i < MaxLayers; ++i)
		{
			StringView name = LayerSystem::GetLayerName(static_cast<u8>(i));
			if (!name.Empty())
			{
				activeLayers.EmplaceBack(LayerInfo{static_cast<u8>(i), name});
			}
		}

		if (activeLayers.Empty()) return;

		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("Layer Collision Matrix");
		ImGui::Spacing();

		u32 count = activeLayers.Size();
		f32 scaleFactor = ImGui::GetStyle().ScaleFactor;
		f32 cellSize = 28.0f * scaleFactor;
		f32 labelWidth = 120.0f * scaleFactor;
		f32 fontSize = ImGui::GetFontSize();

		// Build RID lookup: layer index -> CollisionMatrixItem RID
		HashMap<u32, RID> matrixItemByIndex;
		if (ResourceObject settings = Resources::Read(rid))
		{
			settings.IterateSubObjectList(PhysicsSettings::CollisionMatrix, [&](RID itemRID)
			{
				if (ResourceObject item = Resources::Read(itemRID))
				{
					u32 idx = static_cast<u32>(item.GetUInt(CollisionMatrixItem::Index));
					matrixItemByIndex.Insert(idx, itemRID);
				}
			});
		}

		// Calculate header height from longest layer name
		f32 maxTextWidth = 0.0f;
		for (u32 i = 0; i < count; ++i)
		{
			ImVec2 textSize = ImGui::CalcTextSize(activeLayers[i].name.CStr());
			if (textSize.x > maxTextWidth) maxTextWidth = textSize.x;
		}
		f32 headerHeight = maxTextWidth + 12.0f * scaleFactor;

		// Draw vertical column headers (rotated -90 degrees)
		ImVec2 startPos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(labelWidth + count * cellSize, headerHeight));

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (u32 col = 0; col < count; ++col)
		{
			f32 x = startPos.x + labelWidth + col * cellSize;
			f32 y = startPos.y + headerHeight - 4.0f * scaleFactor;

			int vtxStart = drawList->VtxBuffer.Size;
			drawList->AddText(ImVec2(x, y), IM_COL32(200, 200, 200, 255), activeLayers[col].name.CStr());
			int vtxEnd = drawList->VtxBuffer.Size;

			for (int i = vtxStart; i < vtxEnd; i++)
			{
				f32 dx = drawList->VtxBuffer[i].pos.x - x;
				f32 dy = drawList->VtxBuffer[i].pos.y - y;
				drawList->VtxBuffer[i].pos.x = x + dy;
				drawList->VtxBuffer[i].pos.y = y - dx;
			}
		}

		// Draw rows (staircase triangle — headers align with row 0)
		for (u32 row = 0; row < count; ++row)
		{
			u8 layerA = activeLayers[row].index;

			ImGui::Text("%s", activeLayers[row].name.CStr());
			ImGui::SameLine(labelWidth);

			for (u32 col = row; col < count; ++col)
			{
				u8 layerB = activeLayers[col].index;
				bool collides = Physics::GetLayerCollision(layerA, layerB);

				ImGui::PushID(row * MaxLayers + col);
				if (ImGui::Checkbox("##cm", &collides))
				{
					Physics::SetLayerCollision(layerA, layerB, collides);

					auto itA = matrixItemByIndex.Find(layerA);
					if (itA != matrixItemByIndex.end())
					{
						ResourceObject itemA = Resources::Write(itA->second);
						itemA.SetUInt(CollisionMatrixItem::Value, Physics::GetCollisionMask(layerA));
						itemA.Commit();
					}

					if (layerA != layerB)
					{
						auto itB = matrixItemByIndex.Find(layerB);
						if (itB != matrixItemByIndex.end())
						{
							ResourceObject itemB = Resources::Write(itB->second);
							itemB.SetUInt(CollisionMatrixItem::Value, Physics::GetCollisionMask(layerB));
							itemB.Commit();
						}
					}
				}
				ImGui::PopID();

				if (col + 1 < count)
				{
					ImGui::SameLine(labelWidth + (col - row + 1) * cellSize);
				}
			}
		}
	}


	void SettingsWindow::OpenAction(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<SettingsWindow>(IntToPtr(eventData.userData));
	}

	void SettingsWindow::RegisterType(NativeReflectType<SettingsWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Editor Settings", .priority = 1000, .action = OpenAction, .userData = TypeInfo<EditorSettings>::ID()});
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Project Settings", .priority = 1010, .action = OpenAction, .userData = TypeInfo<ProjectSettings>::ID()});
	}
}
