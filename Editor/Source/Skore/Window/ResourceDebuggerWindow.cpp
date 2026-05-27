#include "Skore/Window/ResourceDebuggerWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"

namespace Skore
{
	namespace
	{
		const char* FieldTypeName(ResourceFieldType type)
		{
			switch (type)
			{
				case ResourceFieldType::None:           return "None";
				case ResourceFieldType::Bool:           return "Bool";
				case ResourceFieldType::Int:            return "Int";
				case ResourceFieldType::UInt:           return "UInt";
				case ResourceFieldType::Float:          return "Float";
				case ResourceFieldType::String:         return "String";
				case ResourceFieldType::Vec2:           return "Vec2";
				case ResourceFieldType::Vec3:           return "Vec3";
				case ResourceFieldType::Vec4:           return "Vec4";
				case ResourceFieldType::Quat:           return "Quat";
				case ResourceFieldType::Mat4:           return "Mat4";
				case ResourceFieldType::Color:          return "Color";
				case ResourceFieldType::Enum:           return "Enum";
				case ResourceFieldType::Blob:           return "Blob";
				case ResourceFieldType::Reference:      return "Reference";
				case ResourceFieldType::ReferenceArray: return "ReferenceArray";
				case ResourceFieldType::SubObject:      return "SubObject";
				case ResourceFieldType::SubObjectList:  return "SubObjectList";
				case ResourceFieldType::Buffer:         return "Buffer";
				default:                                return "Unknown";
			}
		}

		String GetSubTypeName(TypeID subType)
		{
			if (subType == TypeID{}) return {};

			if (ResourceType* resType = Resources::FindTypeByID(subType))
			{
				String simpleName = resType->GetSimpleName();
				if (!simpleName.Empty()) return simpleName;
				return resType->GetName();
			}

			if (ReflectType* refType = Reflection::FindTypeById(subType))
			{
				StringView simpleName = refType->GetSimpleName();
				if (!simpleName.Empty()) return String{simpleName};
				return String{refType->GetName()};
			}

			return {};
		}

		String GetResourceLabel(RID rid)
		{
			StringView path = Resources::GetPath(rid);
			if (!path.Empty())
			{
				char buf[256];
				snprintf(buf, sizeof(buf), "RID:%llu (%.*s)", static_cast<unsigned long long>(rid.id), static_cast<int>(path.Size()), path.Data());
				return String{buf};
			}
			char buf[64];
			snprintf(buf, sizeof(buf), "RID:%llu", static_cast<unsigned long long>(rid.id));
			return String{buf};
		}

		bool DrawRIDLink(RID rid, const char* prefix = nullptr)
		{
			String label = prefix ? String{prefix} : GetResourceLabel(rid);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
			bool clicked = ImGui::SmallButton(label.CStr());
			ImGui::PopStyleColor();
			return clicked;
		}
	}

	const char* ResourceDebuggerWindow::GetTitle() const
	{
		return "Resource Debugger";
	}

	void ResourceDebuggerWindow::Init(VoidPtr userData)
	{
		if (userData)
		{
			RID rid = *static_cast<RID*>(userData);
			instanceHistory.Clear();
			selectedInstance = {};
			NavigateToInstance(rid);
		}
	}

	void ResourceDebuggerWindow::NavigateToInstance(RID rid)
	{
		if (selectedInstance.id)
		{
			instanceHistory.EmplaceBack(selectedInstance);
		}
		selectedInstance = rid;
		switchToInstanceTab = true;
	}

	void ResourceDebuggerWindow::Draw(bool& open)
	{
		ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
		if (!ImGuiBegin(this, &open))
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("##debugger_tabs"))
		{
			ImGuiTabItemFlags typesFlags = 0;
			if (switchToTypesTab)
			{
				typesFlags = ImGuiTabItemFlags_SetSelected;
				switchToTypesTab = false;
			}

			if (ImGui::BeginTabItem("Types", nullptr, typesFlags))
			{
				DrawTypesTab(id);
				ImGui::EndTabItem();
			}

			ImGuiTabItemFlags instanceFlags = 0;
			if (switchToInstanceTab)
			{
				instanceFlags = ImGuiTabItemFlags_SetSelected;
				switchToInstanceTab = false;
			}

			if (ImGui::BeginTabItem("Instance", nullptr, instanceFlags))
			{
				DrawInstanceTab(id);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void ResourceDebuggerWindow::DrawTypesTab(u32 id)
	{
		Array<ResourceType*> types = Resources::GetTypes();
		if (types.Empty())
		{
			ImGui::TextUnformatted("No resource types registered.");
			return;
		}

		// --- Left panel: type list with search ---
		f32 leftPanelWidth = 220.0f * ImGui::GetStyle().ScaleFactor;
		ImGui::BeginChild("##type_list", ImVec2(leftPanelWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

		ImGui::SetNextItemWidth(-1);
		ImGuiSearchInputText(id + 1, searchFilter);
		ImGui::Separator();

		ImGuiTextFilter filter;
		if (!searchFilter.Empty())
		{
			memset(filter.InputBuf, 0, sizeof(filter.InputBuf));
			memcpy(filter.InputBuf, searchFilter.CStr(), Math::Min(searchFilter.Size(), sizeof(filter.InputBuf) - 1));
			filter.Build();
		}

		for (ResourceType* type : types)
		{
			if (type == nullptr) continue;

			String displayName = type->GetSimpleName();
			if (displayName.Empty()) displayName = type->GetName();

			if (!searchFilter.Empty() && !filter.PassFilter(displayName.CStr()))
			{
				continue;
			}

			bool isSelected = (selectedType == type);
			if (ImGui::Selectable(displayName.CStr(), isSelected))
			{
				selectedType = type;
			}
		}

		ImGui::EndChild();

		ImGui::SameLine();

		// --- Right panel: type details ---
		ImGui::BeginChild("##type_details", ImVec2(0, 0));

		if (selectedType != nullptr)
		{
			// --- Type info ---
			if (ImGui::CollapsingHeader("Type Info", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::BeginTable("##type_info", 2, ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f * ImGui::GetStyle().ScaleFactor);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Full Name");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(selectedType->GetName().CStr());

					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Type ID");
					ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(selectedType->GetID()));

					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Version");
					ImGui::TableNextColumn(); ImGui::Text("%u", selectedType->GetVersion());

					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Fields");
					ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(selectedType->GetFields().Size()));

					if (selectedType->GetReflectType())
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted("Reflect Type");
						ImGui::TableNextColumn(); ImGui::TextUnformatted(selectedType->GetReflectType()->GetName().Data());
					}

					ImGui::EndTable();
				}
			}

			// --- Fields ---
			Span<ResourceField*> fields = selectedType->GetFields();
			if (fields.Size() > 0 && ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen))
			{
				static ImGuiTableFlags fieldTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

				if (ImGui::BeginTable("##fields_table", 5, fieldTableFlags))
				{
					ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 40.0f * ImGui::GetStyle().ScaleFactor);
					ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthFixed, 110.0f * ImGui::GetStyle().ScaleFactor);
					ImGui::TableSetupColumn("Sub-Type", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Size",  ImGuiTableColumnFlags_WidthFixed, 50.0f * ImGui::GetStyle().ScaleFactor);
					ImGui::TableHeadersRow();

					for (ResourceField* field : fields)
					{
						if (field == nullptr) continue;

						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Text("%u", field->GetIndex());

						ImGui::TableNextColumn();
						ImGui::TextUnformatted(field->GetName().Data());

						ImGui::TableNextColumn();
						ImGui::TextUnformatted(FieldTypeName(field->GetType()));

						ImGui::TableNextColumn();
						String subTypeName = GetSubTypeName(field->GetSubType());
						ImGui::TextUnformatted(subTypeName.CStr());

						ImGui::TableNextColumn();
						ImGui::Text("%u", field->GetSize());
					}

					ImGui::EndTable();
				}
			}

			// --- Instances ---
			Array<RID> instances = Resources::GetResourcesByType(selectedType);
			char instanceHeader[64];
			snprintf(instanceHeader, sizeof(instanceHeader), "Instances (%llu)", static_cast<unsigned long long>(instances.Size()));

			if (ImGui::CollapsingHeader(instanceHeader, ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (instances.Empty())
				{
					ImGui::TextDisabled("No instances of this type.");
				}
				else
				{
					static ImGuiTableFlags instanceTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
						| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

					if (ImGui::BeginTable("##instances_table", 4, instanceTableFlags, ImVec2(0, 0)))
					{
						ImGui::TableSetupColumn("RID",  ImGuiTableColumnFlags_WidthFixed, 60.0f * ImGui::GetStyle().ScaleFactor);
						ImGui::TableSetupColumn("UUID", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Ver.", ImGuiTableColumnFlags_WidthFixed, 40.0f * ImGui::GetStyle().ScaleFactor);
						ImGui::TableHeadersRow();

						for (RID rid : instances)
						{
							ImGui::TableNextRow();

							ImGui::TableNextColumn();
							ImGui::PushID(static_cast<int>(rid.id));
							char ridLabel[32];
							snprintf(ridLabel, sizeof(ridLabel), "%llu", static_cast<unsigned long long>(rid.id));
							if (ImGui::Selectable(ridLabel, false, ImGuiSelectableFlags_SpanAllColumns))
							{
								NavigateToInstance(rid);
							}
							ImGui::PopID();

							ImGui::TableNextColumn();
							UUID uuid = Resources::GetUUID(rid);
							if (uuid)
							{
								String uuidStr = uuid.ToString();
								ImGui::TextUnformatted(uuidStr.CStr());
							}
							else
							{
								ImGui::TextDisabled("-");
							}

							ImGui::TableNextColumn();
							StringView path = Resources::GetPath(rid);
							if (!path.Empty())
								ImGui::TextUnformatted(path.Data(), path.Data() + path.Size());
							else
								ImGui::TextDisabled("-");

							ImGui::TableNextColumn();
							ImGui::Text("%llu", static_cast<unsigned long long>(Resources::GetVersion(rid)));
						}

						ImGui::EndTable();
					}
				}
			}
		}
		else
		{
			ImGui::TextDisabled("Select a resource type from the list.");
		}

		ImGui::EndChild();
	}

	void ResourceDebuggerWindow::DrawInstanceTab(u32 id)
	{
		if (!selectedInstance.id)
		{
			ImGui::TextDisabled("No instance selected. Select one from the Types tab.");
			return;
		}

		ResourceType* type = Resources::GetType(selectedInstance);
		if (type == nullptr)
		{
			ImGui::TextDisabled("Instance type not found.");
			return;
		}

		// --- Navigation ---
		if (!instanceHistory.Empty())
		{
			if (ImGui::SmallButton(ICON_FA_ARROW_LEFT " Back"))
			{
				selectedInstance = instanceHistory.Back();
				instanceHistory.PopBack();
			}
		}

		// --- Resource metadata table ---
		String typeName = type->GetSimpleName();
		if (typeName.Empty()) typeName = type->GetName();

		ImGui::SeparatorText("Resource Info");

		if (ImGui::BeginTable("##resource_meta", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f * ImGui::GetStyle().ScaleFactor);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				// RID
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("RID");
				ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(selectedInstance.id));

				// Type (link to Types tab)
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Type");
				ImGui::TableNextColumn();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
				if (ImGui::SmallButton(typeName.CStr()))
				{
					SelectTypeInTab(type);
				}
				ImGui::PopStyleColor();

				// UUID
				UUID uuid = Resources::GetUUID(selectedInstance);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("UUID");
				ImGui::TableNextColumn();
				if (uuid)
				{
					String uuidStr = uuid.ToString();
					ImGui::TextUnformatted(uuidStr.CStr());
				}
				else
				{
					ImGui::TextDisabled("-");
				}

				// Path
				StringView path = Resources::GetPath(selectedInstance);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Path");
				ImGui::TableNextColumn();
				if (!path.Empty())
					ImGui::TextUnformatted(path.Data(), path.Data() + path.Size());
				else
					ImGui::TextDisabled("-");

				// Version
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Version");
				ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(Resources::GetVersion(selectedInstance)));

				// Parent
				RID parent = Resources::GetParent(selectedInstance);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Parent");
				ImGui::TableNextColumn();
				if (parent.id)
				{
					if (DrawRIDLink(parent))
					{
						NavigateToInstance(parent);
					}
				}
				else
				{
					ImGui::TextDisabled("-");
				}

				// Prototype
				RID prototype = Resources::GetPrototype(selectedInstance);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Prototype");
				ImGui::TableNextColumn();
				if (prototype.id)
				{
					if (DrawRIDLink(prototype))
					{
						NavigateToInstance(prototype);
					}
				}
				else
				{
					ImGui::TextDisabled("-");
				}

				ImGui::EndTable();
		}

		ImGui::SeparatorText("Values");

		DrawInstanceFields(selectedInstance);
	}

	void ResourceDebuggerWindow::DrawInstanceFields(RID rid)
	{
		if (!Resources::HasValue(rid))
		{
			ImGui::TextDisabled("Instance has no committed value.");
			return;
		}

		ResourceType* type = Resources::GetType(rid);
		if (type == nullptr) return;

		Span<ResourceField*> fields = type->GetFields();
		if (fields.Size() == 0)
		{
			ImGui::TextDisabled("Type has no fields.");
			return;
		}

		ResourceObject object = Resources::Read(rid);
		if (!object) return;

		static ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

		if (!ImGui::BeginTable("##instance_fields", 3, tableFlags))
		{
			return;
		}

		ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 160.0f * ImGui::GetStyle().ScaleFactor);
		ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthFixed, 110.0f * ImGui::GetStyle().ScaleFactor);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (ResourceField* field : fields)
		{
			if (field == nullptr) continue;

			u32 idx = field->GetIndex();
			ResourceFieldType fieldType = field->GetType();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(field->GetName().Data());

			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", FieldTypeName(fieldType));

			ImGui::TableNextColumn();

			if (!object.HasValue(idx))
			{
				ImGui::TextDisabled("(no value)");
				continue;
			}

			ImGui::PushID(static_cast<int>(idx));

			switch (fieldType)
			{
				case ResourceFieldType::Bool:
					ImGui::TextUnformatted(object.GetBool(idx) ? "true" : "false");
					break;

				case ResourceFieldType::Int:
					ImGui::Text("%lld", static_cast<long long>(object.GetInt(idx)));
					break;

				case ResourceFieldType::UInt:
					ImGui::Text("%llu", static_cast<unsigned long long>(object.GetUInt(idx)));
					break;

				case ResourceFieldType::Float:
					ImGui::Text("%.4f", object.GetFloat(idx));
					break;

				case ResourceFieldType::String:
				{
					StringView str = object.GetString(idx);
					if (str.Empty())
						ImGui::TextDisabled("(empty)");
					else
						ImGui::TextUnformatted(str.Data(), str.Data() + str.Size());
					break;
				}

				case ResourceFieldType::Vec2:
				{
					Vec2 v = object.GetVec2(idx);
					ImGui::Text("(%.3f, %.3f)", v.x, v.y);
					break;
				}

				case ResourceFieldType::Vec3:
				{
					Vec3 v = object.GetVec3(idx);
					ImGui::Text("(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
					break;
				}

				case ResourceFieldType::Vec4:
				{
					Vec4 v = object.GetVec4(idx);
					ImGui::Text("(%.3f, %.3f, %.3f, %.3f)", v.x, v.y, v.z, v.w);
					break;
				}

				case ResourceFieldType::Quat:
				{
					Quat q = object.GetQuat(idx);
					ImGui::Text("(%.3f, %.3f, %.3f, %.3f)", q.x, q.y, q.z, q.w);
					break;
				}

				case ResourceFieldType::Color:
				{
					Color c = object.GetColor(idx);
					ImVec4 col(c.FloatRed(), c.FloatGreen(), c.FloatBlue(), c.FloatAlfa());
					ImGui::ColorButton("##color", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
					ImGui::SameLine();
					ImGui::Text("(%u, %u, %u, %u)", c.red, c.green, c.blue, c.alpha);
					break;
				}

				case ResourceFieldType::Enum:
					ImGui::Text("%lld", static_cast<long long>(object.GetEnum(idx)));
					break;

				case ResourceFieldType::Reference:
				{
					RID ref = object.GetReference(idx);
					if (ref.id)
					{
						if (DrawRIDLink(ref))
						{
							NavigateToInstance(ref);
						}
					}
					else
					{
						ImGui::TextDisabled("(null)");
					}
					break;
				}

				case ResourceFieldType::SubObject:
				{
					RID sub = object.GetSubObject(idx);
					if (sub.id)
					{
						if (DrawRIDLink(sub))
						{
							NavigateToInstance(sub);
						}
					}
					else
					{
						ImGui::TextDisabled("(null)");
					}
					break;
				}

				case ResourceFieldType::ReferenceArray:
				{
					Span<RID> refs = object.GetReferenceArray(idx);
					char treeLabel[64];
					snprintf(treeLabel, sizeof(treeLabel), "[%llu refs]", static_cast<unsigned long long>(refs.Size()));

					if (refs.Size() > 0 && ImGui::TreeNode("##ref_arr", "%s", treeLabel))
					{
						for (usize i = 0; i < refs.Size(); ++i)
						{
							ImGui::PushID(static_cast<int>(i));
							if (refs[i].id)
							{
								if (DrawRIDLink(refs[i]))
								{
									NavigateToInstance(refs[i]);
								}
							}
							else
							{
								ImGui::TextDisabled("(null)");
							}
							ImGui::PopID();
						}
						ImGui::TreePop();
					}
					else if (refs.Size() == 0)
					{
						ImGui::TextDisabled("(empty)");
					}
					break;
				}

				case ResourceFieldType::SubObjectList:
				{
					Span<RID> subs = object.GetSubObjectList(idx);
					char treeLabel[64];
					snprintf(treeLabel, sizeof(treeLabel), "[%llu sub-objects]", static_cast<unsigned long long>(subs.Size()));

					if (subs.Size() > 0 && ImGui::TreeNode("##sub_list", "%s", treeLabel))
					{
						for (usize i = 0; i < subs.Size(); ++i)
						{
							ImGui::PushID(static_cast<int>(i));
							if (subs[i].id)
							{
								if (DrawRIDLink(subs[i]))
								{
									NavigateToInstance(subs[i]);
								}
							}
							else
							{
								ImGui::TextDisabled("(null)");
							}
							ImGui::PopID();
						}
						ImGui::TreePop();
					}
					else if (subs.Size() == 0)
					{
						ImGui::TextDisabled("(empty)");
					}
					break;
				}

				case ResourceFieldType::Blob:
				{
					Span<u8> blob = object.GetBlob(idx);
					ImGui::Text("[%llu bytes]", static_cast<unsigned long long>(blob.Size()));
					break;
				}

				case ResourceFieldType::Buffer:
					ImGui::TextUnformatted("[buffer]");
					break;

				case ResourceFieldType::Mat4:
					ImGui::TextUnformatted("[mat4]");
					break;

				default:
					ImGui::TextDisabled("(?)");
					break;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	void ResourceDebuggerWindow::SelectTypeInTab(ResourceType* type)
	{
		selectedType = type;
		switchToTypesTab = true;
	}

	void ResourceDebuggerWindow::RegisterType(NativeReflectType<ResourceDebuggerWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Resource Debugger", .action = Open});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::Center,
			.order = 50
		});
	}

	void ResourceDebuggerWindow::Open(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<ResourceDebuggerWindow>();
	}

	void ResourceDebuggerWindow::InspectResource(RID rid)
	{
		if (!rid.id) return;
		Editor::GetActiveWorkspace()->OpenWindow<ResourceDebuggerWindow>(&rid);
	}
}
