#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "imgui_internal.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/LayerSystem.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::FieldRenders");
	}

	template <typename... T>
	bool CanDrawField(const ImGuiDrawFieldDrawCheck& check)
	{
		if (check.fieldProps.isEnum) return false;
		if (check.HasAttribute<UILayerMaskProperty>()) return false;

		return ((check.fieldProps.typeId == TypeInfo<T>::ID()) || ...);
	}

	void DrawVecField(const ImGuiDrawFieldContext& context, const char* fieldName, float& value, bool& updated, bool& updatedFinished, u32 color = 0, f32 speed = 0.005f)
	{
		ImGui::TableNextColumn();

		char buffer[20]{};
		sprintf(buffer, "##%llu", reinterpret_cast<usize>(&value));

		ImGui::BeginHorizontal(buffer);
		ImGui::Text("%s", fieldName);
		ImGui::Spring();
		ImGui::SetNextItemWidth(-1);
		if (color != 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Border, color);
		}

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		if (context.reflectField != nullptr && context.reflectField->GetAttribute<UISliderProperty>())
		{
			if (const UISliderProperty* prop = context.reflectField->GetAttribute<UISliderProperty>())
			{
				if (ImGui::SliderFloat(buffer, &value, prop->minValue, prop->maxValue, prop->format ? prop->format : "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					updated = true;
				}
			}
		}
		else
		{
			if (ImGui::InputFloat(buffer, &value))
			{
				updated = true;
			}
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			updatedFinished = true;
		}

		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}

		if (color != 0)
		{
			ImGui::PopStyleColor();
		}

		ImGui::EndHorizontal();
	}

	void DrawVec2Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		Vec2& vec2 = *static_cast<Vec2*>(value);

		if (ImGui::BeginTable("##vec2-table", 2))
		{
			DrawVecField(context, "X", vec2.x, updated, updatedFinished, IM_COL32(138, 46, 61, 255));
			DrawVecField(context, "Y", vec2.y, updated, updatedFinished, IM_COL32(87, 121, 26, 255));
			ImGui::EndTable();
		}
	}

	void DrawVec3Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		Vec3& vec3 = *static_cast<Vec3*>(value);
		if (ImGui::BeginTable("##vec3-table", 3))
		{
			DrawVecField(context, "X", vec3.x, updated, updatedFinished, IM_COL32(138, 46, 61, 255));
			DrawVecField(context, "Y", vec3.y, updated, updatedFinished, IM_COL32(87, 121, 26, 255));
			DrawVecField(context, "Z", vec3.z, updated, updatedFinished, IM_COL32(43, 86, 138, 255));
			ImGui::EndTable();
		}
	}

	void DrawVec4Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		Vec4& vec4 = *static_cast<Vec4*>(value);
		if (ImGui::BeginTable("##vec4-table", 4))
		{
			DrawVecField(context, "X", vec4.x, updated, updatedFinished, IM_COL32(138, 46, 61, 255));
			DrawVecField(context, "Y", vec4.y, updated, updatedFinished, IM_COL32(87, 121, 26, 255));
			DrawVecField(context, "Z", vec4.z, updated, updatedFinished, IM_COL32(43, 86, 138, 255));
			DrawVecField(context, "W", vec4.w, updated, updatedFinished, IM_COL32(84, 74, 119, 255));
			ImGui::EndTable();
		}
	}


	void DrawQuatField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		static int rotationMode = 0;

		Quat& quat = *static_cast<Quat*>(value);

		if (rotationMode == 0)
		{
			Vec3 euler = Math::Degrees(Quat::EulerAngles(quat));

			if (ImGui::BeginTable("##vec3-table", 3))
			{
				DrawVecField(context, "X", euler.x, updated, updatedFinished, IM_COL32(138, 46, 61, 255));
				DrawVecField(context, "Y", euler.y, updated, updatedFinished, IM_COL32(87, 121, 26, 255));
				DrawVecField(context, "Z", euler.z, updated, updatedFinished, IM_COL32(43, 86, 138, 255));
				ImGui::EndTable();
			}

			if (updated)
			{
				quat = Quat{Math::Radians(euler)};
			}
		}
		else
		{
			if (ImGui::BeginTable("##quat-table", 4))
			{
				DrawVecField(context, "X", quat.x, updated, updatedFinished, IM_COL32(138, 46, 61, 255));
				DrawVecField(context, "Y", quat.y, updated, updatedFinished, IM_COL32(87, 121, 26, 255));
				DrawVecField(context, "Z", quat.z, updated, updatedFinished, IM_COL32(43, 86, 138, 255));
				DrawVecField(context, "W", quat.w, updated, updatedFinished, IM_COL32(84, 74, 119, 255));
				ImGui::EndTable();
			}
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			if (ImGuiCurrentTableHovered())
			{
				ImGui::OpenPopup("open-rotation-mode-popup");
			}
		}

		bool popupOpenSettings = ImGuiBeginPopupMenu("open-rotation-mode-popup", 0, false);
		if (popupOpenSettings)
		{
			if (ImGui::MenuItem("Euler", "", rotationMode == 0))
			{
				rotationMode = 0;
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Quaternion", "", rotationMode == 1))
			{
				rotationMode = 1;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGuiEndPopupMenu(popupOpenSettings);
	}


	void DrawColorField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		Color&       color = *static_cast<Color*>(value);
		const ImVec4 colV4(color.FloatRed(), color.FloatGreen(), color.FloatBlue(), color.FloatAlfa());

		char str[40];
		sprintf(str, "###colorid%llu", context.id);

		char picker[40];
		sprintf(picker, "###picker_id%llu", context.id);

		ImGui::SetNextItemWidth(-1);
		if (ImGui::ColorButton(str, colV4, 0, ImVec2(ImGui::CalcItemWidth(), 0)))
		{
			ImGui::OpenPopup(picker);
		}

		if (ImGui::BeginPopup(picker))
		{
			ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf |
				ImGuiColorEditFlags_AlphaBar;

			static f32 col[4] = {
				color.FloatRed(),
				color.FloatGreen(),
				color.FloatBlue(),
				color.FloatAlfa()
			};

			if (ImGui::ColorPicker4("##picker", col, flags))
			{
				updated = true;
				Color::FromVec4(color, {col[0], col[1], col[2], col[3]});
			}

			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				updatedFinished = true;
			}

			ImGui::EndPopup();
		}
	}

	bool CanDrawEnumField(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.fieldProps.isEnum && check.reflectFieldType != nullptr;
	}


	void DrawEnumField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		char str[30];
		sprintf(str, "###enumid%llu", context.id);

		ImGui::SetNextItemWidth(-1);
		ReflectValue* reflectValue = context.reflectFieldType->FindValue(value);

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		if (ImGui::BeginCombo(str, reflectValue != nullptr ? reflectValue->GetDesc().CStr() : ""))
		{
			for (const auto& valueHandler : context.reflectFieldType->GetValues())
			{
				if (ImGui::Selectable(valueHandler->GetDesc().CStr()))
				{
					valueHandler->SetToPointer(value);
					updated = true;
					updatedFinished = true;
				}
			}
			ImGui::EndCombo();
		}

		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}
	}

	void DrawBoolField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		char str[30];
		sprintf(str, "###txtid%llu", context.id);

		bool& boolValue = *static_cast<bool*>(value);

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		if (ImGui::Checkbox(str, &boolValue))
		{
			updated = true;
			updatedFinished = true;
		}

		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}
	}


	void DrawStringField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		String& strValue = *static_cast<String*>(value);

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		ImGui::SetNextItemWidth(-1);

		bool multiline = false;

		if (context.reflectField != nullptr && context.reflectField->GetAttribute<UIStringProperty>())
		{
			if (const UIStringProperty* prop = context.reflectField->GetAttribute<UIStringProperty>())
			{
				multiline = prop->multiline;
			}
		}

		if (multiline)
		{
			if (ImGuiInputTextMultiline(context.id, strValue, ImVec2(0, 100 * ImGui::GetStyle().ScaleFactor)))
			{
				updated = true;
			}
		}
		else
		{
			if (ImGuiInputText(context.id, strValue))
			{
				updated = true;
			}
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			updatedFinished = true;
		}


		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}
	}


	void DrawFloatField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		ImGuiDataType dataType = ImGuiDataType_Double;

		if (context.reflectField && !context.resourceField)
		{
			if (context.reflectField->GetProps().typeId == TypeInfo<f32>::ID())
			{
				dataType = ImGuiDataType_Float;
			}
		}

		char str[30];
		sprintf(str, "###%llu", context.id);

		float               step = 0.0f;
		float               stepFast = 0.0f;
		const char*         format = "%.3f";
		ImGuiInputTextFlags flags = 0;

		ImGui::SetNextItemWidth(-1);

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		if (context.reflectField != nullptr && context.reflectField->GetAttribute<UISliderProperty>())
		{
			if (const UISliderProperty* prop = context.reflectField->GetAttribute<UISliderProperty>())
			{
				if (ImGui::SliderScalar(str, dataType, value, &prop->minValue, &prop->maxValue, prop->format ? prop->format : "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					updated = true;
				}
			}
		}
		else
		{
			if (ImGui::InputScalar(str, dataType, value, step > 0.0f ? &step : nullptr, stepFast > 0.0f ? &stepFast : nullptr, format, flags))
			{
				updated = true;
			}
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			updatedFinished = true;
		}

		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}
	}

	template<typename T>
	void DrawFieldInt(const ImGuiDrawFieldContext& context, ImGuiDataType dataType, T* value, bool& updated, bool& updatedFinished)
	{
		char str[30];
		sprintf(str, "###%llu", context.id);

		i32 step = 0;
		i32 stepFast = 0;
		ImGuiInputTextFlags flags = 0;

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputScalar(str, dataType, static_cast<VoidPtr>(value), step > 0 ? &step : NULL, stepFast > 0 ? &stepFast : NULL, "%d", flags))
		{
			updated = true;
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			updatedFinished = true;
		}

		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}
	}


	void DrawU32Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		DrawFieldInt(context, ImGuiDataType_U32, static_cast<u32*>(value), updated, updatedFinished);
	}

	void DrawU64Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		DrawFieldInt(context, ImGuiDataType_U64, static_cast<u64*>(value), updated, updatedFinished);
	}

	void DrawI32Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		DrawFieldInt(context, ImGuiDataType_S32, static_cast<i32*>(value), updated, updatedFinished);
	}

	void DrawI64Field(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		DrawFieldInt(context, ImGuiDataType_S64, static_cast<i64*>(value), updated, updatedFinished);
	}

	template <typename T>
	void DrawResource(const ImGuiDrawFieldContext& context, const RID rid, u64 id, TypeID typeId, T&& func)
	{
		bool isEntityDraw = typeId == TypeInfo<Entity>::ID();

		char pushStr[30];
		sprintf(pushStr, "###push%llu", id);

		bool openPopup = false;

		String name = ResourceAssets::GetAssetName(rid);

		ImGui::SetNextItemWidth(-22 * ImGui::GetStyle().ScaleFactor);
		ImGui::PushID(pushStr);

		{
			if (context.overridden)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
			}

			ImGuiInputTextReadOnly(id, name);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
				{
					if (payload->IsDataType(SK_ASSET_PAYLOAD))
					{
						if (AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data))
						{
							TypeID assetType = Resources::GetType(assetPayload->asset)->GetID();
							if (assetType == typeId)
							{
								if (ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
								{
									func(assetPayload->asset);
								}
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered())
			{
				RID assetToSelect = {};
				if (isEntityDraw)
				{
					SceneEditor* sceneEditor = Editor::GetActiveWorkspace()->GetSceneEditor();
					if (Resources::IsParentOf(sceneEditor->GetOpenedResource(), rid))
					{
						sceneEditor->SelectEntity(rid, true);
					}
					else
					{
						assetToSelect = rid;
					}
				}

				if (assetToSelect)
				{
					//TODO
				}
			}

			if (context.overridden)
			{
				ImGui::PopStyleColor();
			}
		}

		ImGui::SameLine(0, 0);
		auto size = ImGui::GetItemRectSize();

		if (ImGui::Button(ICON_FA_CIRCLE_DOT, ImVec2{size.y, size.y}))
		{
			openPopup = true;
		}
		ImGui::PopID();

		ImGuiResourceSelectionPopup(id, typeId, context.rid, openPopup, [](RID rid, VoidPtr userData)
		{
			(*static_cast<T*>(userData))(rid);
		}, &func);
	}

	bool CanDrawResourceField(const ImGuiDrawFieldDrawCheck& check)
	{
		if (check.IsResourceFieldType(ResourceFieldType::Reference))
		{
			return true;
		}

		//object fields like TypedRID<T> map to references, pointer fields (e.g. Entity*) don't hold a RID value.
		return check.resourceField == nullptr &&
			check.reflectField != nullptr &&
			!check.fieldProps.isPointer &&
			check.reflectField->GetResourceFieldInfo().type == ResourceFieldType::Reference;
	}

	void DrawResourceField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		TypeID subType = {};
		if (context.resourceField)
		{
			subType = context.resourceField->GetSubType();
		}
		else if (context.reflectField)
		{
			subType = context.reflectField->GetResourceFieldInfo().subType;
		}

		RID& rid = *static_cast<RID*>(value);
		DrawResource(context, rid, context.id, subType, [&](RID newRid)
		{
			ImGuiCommitFieldChanges(context, &newRid, sizeof(RID));
		});
	}

	bool CanDrawArrayField(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.fieldProps.typeApi == TypeInfo<ArrayApi>::ID() && !check.IsResourceFieldType(ResourceFieldType::ReferenceArray);
	}


	// namespace
	// {
	// 	struct ArrayCustomContext
	// 	{
	// 		Array<ImGuiFieldRenderer> draws;
	// 		VoidPtr                   instance;
	// 		ReflectType*              type;
	// 	};
	// }
	//
	// void ArrayDestroyCustomContext(VoidPtr context)
	// {
	// 	DestroyAndFree(static_cast<ArrayCustomContext*>(context));
	// }
	//
	// VoidPtr ArrayCreateCustomContext(const ImGuiDrawFieldDrawCheck& drawCheck)
	// {
	// 	ArrayApi api = {};
	// 	drawCheck.fieldProps.getTypeApi(&api);
	//
	// 	Array<ImGuiFieldRenderer> draws;
	//
	// 	ImGuiDrawFieldDrawCheck check;
	// 	check.fieldProps = ToFieldProps(api.GetProps());
	// 	check.reflectField = nullptr;
	// 	check.reflectFieldType = Reflection::FindTypeById(check.fieldProps.typeId);
	//
	// 	if (drawCheck.resourceFieldType == ResourceFieldType::ReferenceArray)
	// 	{
	// 		check.resourceFieldType = ResourceFieldType::Reference;
	// 	}
	//
	// 	for (ImGuiFieldRenderer fieldRenderer : ImGuiGetFieldRenders())
	// 	{
	// 		if (fieldRenderer.canDrawField(check))
	// 		{
	// 			draws.EmplaceBack(fieldRenderer);
	// 		}
	// 	}
	//
	// 	if (!draws.Empty())
	// 	{
	// 		return Alloc<ArrayCustomContext>(draws, api.Create(), check.reflectFieldType);
	// 	}
	//
	// 	return nullptr;
	// }
	//
	// void DrawArrayField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	// {
	// 	if (!context.customContext) return;
	//
	// 	ArrayCustomContext& customContext = *static_cast<ArrayCustomContext*>(context.customContext);
	//
	// 	bool canAdd = true;
	// 	bool canRemove = true;
	//
	// 	ArrayApi api;
	// 	context.fieldProps.getTypeApi(&api);
	// 	api.Copy(customContext.instance, value);
	// 	usize size = api.Size(customContext.instance);
	//
	// 	TypeProps arrProps = api.GetProps();
	//
	// 	if (context.reflectField != nullptr)
	// 	{
	// 		if (const UIArrayProperty* property = context.reflectField->GetAttribute<UIArrayProperty>())
	// 		{
	// 			canAdd = property->canAdd;
	// 			canRemove = property->canRemove;
	// 		}
	// 	}
	//
	// 	ImGui::BeginDisabled(!canAdd);
	//
	// 	if (ImGui::Button(ICON_FA_PLUS))
	// 	{
	// 		api.PushNew(customContext.instance);
	// 		ImGuiCommitFieldChanges(context, customContext.instance, arrProps.size);
	// 	}
	//
	// 	ImGui::EndDisabled();
	//
	// 	ImGui::SameLine();
	//
	// 	ImGui::BeginDisabled(!canRemove || size == 0);
	//
	// 	if (ImGui::Button(ICON_FA_MINUS))
	// 	{
	// 		api.PopBack(customContext.instance);
	// 		ImGuiCommitFieldChanges(context, customContext.instance, arrProps.size);
	// 	}
	//
	// 	ImGui::EndDisabled();
	//
	// 	struct DrawFieldItemUserData
	// 	{
	// 		usize                        index;
	// 		const ImGuiDrawFieldContext& originalContext;
	// 		ArrayCustomContext&          customContext;
	// 		ArrayApi                     api;
	// 	};
	//
	// 	ImGuiDrawFieldContext fieldContext;
	// 	fieldContext.reflectFieldType = customContext.type;
	// 	fieldContext.callback = [](const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size)
	// 	{
	// 		DrawFieldItemUserData& userData = *static_cast<DrawFieldItemUserData*>(context.userData);
	// 		userData.api.Set(userData.customContext.instance, userData.index, pointer);
	//
	// 		TypeProps arrProps = userData.api.GetProps();
	// 		ImGuiCommitFieldChanges(userData.originalContext, userData.customContext.instance, arrProps.size);
	// 	};
	//
	// 	for (usize i = 0; i < size; ++i)
	// 	{
	// 		DrawFieldItemUserData userData{i, context, customContext, api};
	//
	// 		fieldContext.userData = &userData;
	// 		fieldContext.id = context.id + i;
	//
	// 		ImGui::TableNextColumn();
	// 		ImGui::TableNextColumn();
	//
	// 		for (ImGuiFieldRenderer draw : customContext.draws)
	// 		{
	// 			draw.drawField(fieldContext, api.Get(customContext.instance, i));
	// 		}
	// 	}
	// }

	bool CanDrawReferenceArray(const ImGuiDrawFieldDrawCheck& check)
	{
		if (check.IsResourceFieldType(ResourceFieldType::ReferenceArray))
		{
			return true;
		}

		return check.resourceField == nullptr &&
			check.reflectField != nullptr &&
			check.reflectField->GetResourceFieldInfo().type == ResourceFieldType::ReferenceArray;
	}

	void DrawObjectReferenceArray(const ImGuiDrawFieldContext& context, VoidPtr value)
	{
		//value holds a shallow copy of an Array<TypedRID<T>>, never mutate it in place,
		//commits go through ReflectField::Set with a fresh array instead.
		Array<RID> elements = *static_cast<const Array<RID>*>(value);
		TypeID subType = context.reflectField->GetResourceFieldInfo().subType;

		ImGui::PushID(IntToPtr(context.id));

		if (ImGui::Button(ICON_FA_PLUS))
		{
			Array<RID> newArray = elements;
			newArray.EmplaceBack(RID{});
			ImGuiCommitFieldChanges(context, &newArray, sizeof(Array<RID>));
		}

		ImGui::SameLine();

		ImGui::BeginDisabled(elements.Empty());

		if (ImGui::Button(ICON_FA_MINUS))
		{
			Array<RID> newArray = elements;
			newArray.PopBack();
			ImGuiCommitFieldChanges(context, &newArray, sizeof(Array<RID>));
		}

		ImGui::EndDisabled();

		for (usize i = 0; i < elements.Size(); ++i)
		{
			DrawResource(context, elements[i], context.id + i, subType, [&](RID newRid)
			{
				Array<RID> newArray = elements;
				newArray[i] = newRid;
				ImGuiCommitFieldChanges(context, &newArray, sizeof(Array<RID>));
			});
		}

		ImGui::PopID();
	}

	void DrawReferenceArray(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		if (context.resourceField == nullptr)
		{
			if (context.object && context.reflectField && value)
			{
				DrawObjectReferenceArray(context, value);
			}
			return;
		}

		ResourceObject object = Resources::Read(context.rid);
		Span<RID> elements = object.GetReferenceArray(context.resourceField->GetIndex());

		bool canAdd = true;
		bool canRemove = true;

		// if (context.reflectField != nullptr)
		// {
		// 	if (const UIArrayProperty* property = context.reflectField->GetAttribute<UIArrayProperty>())
		// 	{
		// 		canAdd = property->canAdd;
		// 		canRemove = property->canRemove;
		// 	}
		// }

		ImGui::BeginDisabled(!canAdd);

		if (ImGui::Button(ICON_FA_PLUS))
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope(!context.scopeName.Empty() ? context.scopeName : "Update Field");
			ResourceObject resourceObject = Resources::Write(context.rid);
			resourceObject.AddToReferenceArray(context.resourceField->GetIndex(), {});
			resourceObject.Commit(scope);
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		ImGui::BeginDisabled(!canRemove || elements.Size() == 0);

		if (ImGui::Button(ICON_FA_MINUS))
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope(!context.scopeName.Empty() ? context.scopeName : "Update Field");
			ResourceObject resourceObject = Resources::Write(context.rid);
			resourceObject.RemoveFromReferenceArray(context.resourceField->GetIndex(), elements.Size() - 1);
			resourceObject.Commit(scope);
		}

		ImGui::EndDisabled();

		for (int i = 0; i < elements.Size(); ++i)
		{
			RID rid = elements[i];
			DrawResource(context, rid, context.id + i, context.resourceField->GetSubType(), [&](RID updated)
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope(!context.scopeName.Empty() ? context.scopeName : "Update Field");
				ResourceObject resourceObject = Resources::Write(context.rid);
				resourceObject.SetReferenceArray(context.resourceField->GetIndex(), i, updated);
				resourceObject.Commit(scope);
			});
		}
	}

	bool CanDrawBoneData(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.IsResourceFieldType(ResourceFieldType::SubObject) && check.reflectFieldType && check.reflectFieldType->GetProps().typeId == TypeInfo<BoneNode>::ID();
	}

	void DrawBoneTree(RID bone)
	{
		ImGui::TableNextColumn();

		ResourceObject object = Resources::Read(bone);
		Span<RID> children = object.GetSubObjectList(object.GetIndex("children"));
		String name = String(ICON_FA_BONE).Append(" ").Append(object.GetString(object.GetIndex("name")));

		bool open = false;
		if (!children.Empty())
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			open = ImGuiTreeNode(IntToPtr(bone.id), name.CStr());
		}
		else
		{
			ImGuiTreeLeaf(IntToPtr(bone.id), name.CStr());
		}

		if (open)
		{
			for (RID child : children)
			{
				DrawBoneTree(child);
			}
			ImGui::TreePop();
		}
	}

	void DrawBoneData(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		ResourceObject object = Resources::Read(context.rid);
		RID boneData = object.GetSubObject(context.resourceField->GetIndex());

		if (boneData)
		{
			ImGui::EndTable();
			ImGui::Unindent();
			if (ImGuiTreeNode(IntToPtr(932183821), context.label.CStr()))
			{
				ImGui::Unindent();
				ScopedStyleVar   cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
				ScopedStyleVar   frameRounding(ImGuiStyleVar_FrameRounding, 0);
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleColor borderColor(ImGuiCol_Border, IM_COL32(45, 46, 48, 255));

				ImGuiChildFlags childFlags = ImGuiChildFlags_Borders;
				if (ImGui::BeginChild("bone-data-child", ImVec2(ImGui::GetContentRegionAvail().x, 250 * ImGui::GetStyle().ScaleFactor), childFlags))
				{
					ImGui::Indent();
					static ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;

					if (ImGui::BeginTable("bone-data-child-table", 1, tableFlags))
					{
						ScopedStyleVar padding(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
						ScopedStyleVar spacing(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});

						DrawBoneTree(boneData);

						ImGui::EndTable();
					}
				}
				ImGui::EndChild();
				ImGui::TreePop();
			}
			ImGui::Indent();

			//workaround.
			ImGui::BeginTable("##object-table", 2);
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
		}
	}


	bool CanDrawLayerMaskField(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.HasAttribute<UILayerMaskProperty>();
	}

	void DrawLayerMaskField(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		u64& mask = *static_cast<u64*>(value);

		char str[30];
		sprintf(str, "###lmask%llu", context.id);

		// Build display label
		String label;
		if (mask == AllLayersMask)
		{
			label = "Everything";
		}
		else if (mask == 0)
		{
			label = "Nothing";
		}
		else
		{
			u32 count = 0;
			for (u8 i = 0; i < MaxLayers; ++i)
			{
				if (mask & LayerToMask(i))
				{
					StringView name = LayerSystem::GetLayerName(i);
					if (!name.Empty())
					{
						if (count > 0) label += ", ";
						label += name;
						count++;
					}
				}
			}
			if (count == 0) label = "Mixed";
		}

		if (context.overridden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo(str, label.CStr()))
		{
			// Everything / Nothing shortcuts
			if (ImGui::Selectable("Everything", mask == AllLayersMask))
			{
				mask = AllLayersMask;
				updated = true;
				updatedFinished = true;
			}
			if (ImGui::Selectable("Nothing", mask == 0))
			{
				mask = 0;
				updated = true;
				updatedFinished = true;
			}

			ImGui::Separator();

			for (u8 i = 0; i < MaxLayers; ++i)
			{
				StringView layerName = LayerSystem::GetLayerName(i);
				if (layerName.Empty() && i != 0) continue;

				bool checked = (mask & LayerToMask(i)) != 0;
				String itemLabel = layerName.Empty() ? String("Layer ") + ToString(static_cast<u64>(i)) : String(layerName);
				if (ImGui::Checkbox(itemLabel.CStr(), &checked))
				{
					if (checked)
					{
						mask |= LayerToMask(i);
					}
					else
					{
						mask &= ~LayerToMask(i);
					}
					updated = true;
					updatedFinished = true;
				}
			}
			ImGui::EndCombo();
		}

		if (context.overridden)
		{
			ImGui::PopStyleColor();
		}
	}

	bool CanDrawSubObject(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.IsResourceFieldType(ResourceFieldType::SubObject) && check.resourceField && check.resourceField->GetSubType();
	}

	void DrawSubObject(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		ResourceObject object = Resources::Read(context.rid);
		RID subObjectRid = object.GetSubObject(context.resourceField->GetIndex());
		if (!subObjectRid) return;

		ImGui::EndTable();

		ImGuiDrawResource(ImGuiDrawResourceInfo{
			.rid = subObjectRid,
			.scopeName = context.scopeName,
		});

		ImGui::BeginTable("##object-table", 2);
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
		ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
	}

	bool CanDrawSubObjectList(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.IsResourceFieldType(ResourceFieldType::SubObjectList) && check.resourceField && check.resourceField->GetSubType();
	}

	void DrawSubObjectList(const ImGuiDrawFieldContext& context, VoidPtr value, bool& updated, bool& updatedFinished)
	{
		ResourceObject object = Resources::Read(context.rid);
		Span<RID> subObjects = object.GetSubObjectList(context.resourceField->GetIndex());

		ImGui::EndTable();

		ImGui::BeginDisabled(false);
		if (ImGui::Button(ICON_FA_PLUS))
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope(!context.scopeName.Empty() ? context.scopeName : "Add Item");
			ResourceType* subType = Resources::FindTypeByID(context.resourceField->GetSubType());
			if (subType)
			{
				RID newItem = Resources::Create(subType->GetID(), UUID::RandomUUID());
				Resources::Write(newItem).Commit();
				ResourceObject writeObject = Resources::Write(context.rid);
				writeObject.AddToSubObjectList(context.resourceField->GetIndex(), newItem);
				writeObject.Commit(scope);
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();

		ImGui::BeginDisabled(subObjects.Empty());
		if (ImGui::Button(ICON_FA_MINUS))
		{
			if (!subObjects.Empty())
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope(!context.scopeName.Empty() ? context.scopeName : "Remove Item");
				RID lastItem = subObjects[subObjects.Size() - 1];
				ResourceObject writeObject = Resources::Write(context.rid);
				writeObject.RemoveFromSubObjectList(context.resourceField->GetIndex(), lastItem);
				writeObject.Commit(scope);
				Resources::Destroy(lastItem);
			}
		}
		ImGui::EndDisabled();

		for (u32 i = 0; i < subObjects.Size(); ++i)
		{
			RID subRid = subObjects[i];
			ImGui::PushID(static_cast<int>(i));

			String itemLabel = String(context.label) + " " + ToString(static_cast<u64>(i));
			ImGui::SeparatorText(itemLabel.CStr());
			ImGuiDrawResource(ImGuiDrawResourceInfo{
				.rid = subRid,
				.scopeName = context.scopeName,
			});

			ImGui::PopID();
		}

		ImGui::BeginTable("##object-table", 2);
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
		ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
	}

	void RegisterFieldRenderers()
	{
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawLayerMaskField, .drawField = DrawLayerMaskField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Vec2>, .drawField = DrawVec2Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Vec3>, .drawField = DrawVec3Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Vec4>, .drawField = DrawVec4Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Quat>, .drawField = DrawQuatField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Color>, .drawField = DrawColorField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<f32, f64>, .drawField = DrawFloatField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<u32>, .drawField = DrawU32Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<u64>, .drawField = DrawU64Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<i32>, .drawField = DrawI32Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<i64>, .drawField = DrawI64Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<bool>, .drawField = DrawBoolField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<String>, .drawField = DrawStringField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawEnumField, .drawField = DrawEnumField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawResourceField, .drawField = DrawResourceField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawReferenceArray, .drawField = DrawReferenceArray});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawSubObject, .drawField = DrawSubObject, .hideLabel = true});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawSubObjectList, .drawField = DrawSubObjectList, .hideLabel = true});
	}
}
