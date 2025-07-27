// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "IconsFontAwesome6.h"
#include "ImGui.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Entity.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::FieldRenders");
	}

	template <typename... T>
	bool CanDrawField(const ImGuiDrawFieldDrawCheck& check)
	{
		return ((check.fieldProps.typeId == TypeInfo<T>::ID()) || ...);
	}

	void DrawVecField(const ImGuiDrawFieldContext& context, const char* fieldName, float& value, bool* hasChanged, u32 color = 0, f32 speed = 0.005f)
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

		if (context.overriden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		static f32 changedValue = 0.0;

		if (context.reflectField != nullptr && context.reflectField->GetAttribute<UISliderProperty>())
		{
			if (const UISliderProperty* prop = context.reflectField->GetAttribute<UISliderProperty>())
			{
				if (ImGui::SliderFloat(buffer, &value, prop->minValue, prop->maxValue, prop->format ? prop->format : "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					changedValue = value;
				}
			}
		}
		else
		{
			ImGui::InputFloat(buffer, &value);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			*hasChanged = true;
			value = changedValue;
			changedValue = 0.0;
		}

		if (context.overriden)
		{
			ImGui::PopStyleColor();
		}

		if (color != 0)
		{
			ImGui::PopStyleColor();
		}

		ImGui::EndHorizontal();
	}

	void DrawVec2Field(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		bool hasChanged = false;

		f32  speed = 0.005f;
		Vec2 vec2 = *static_cast<const Vec2*>(value);
		if (ImGui::BeginTable("##vec2-table", 2))
		{
			DrawVecField(context, "X", vec2.x, &hasChanged, IM_COL32(138, 46, 61, 255), speed);
			DrawVecField(context, "Y", vec2.y, &hasChanged, IM_COL32(87, 121, 26, 255), speed);
			ImGui::EndTable();
		}

		if (hasChanged)
		{
			ImGuiCommitFieldChanges(context, &vec2, sizeof(Vec2));
		}
	}

	void DrawVec3Field(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		bool hasChanged = false;

		f32  speed = 0.005f;
		Vec3 vec3 = *static_cast<const Vec3*>(value);
		if (ImGui::BeginTable("##vec3-table", 3))
		{
			DrawVecField(context, "X", vec3.x, &hasChanged, IM_COL32(138, 46, 61, 255), speed);
			DrawVecField(context, "Y", vec3.y, &hasChanged, IM_COL32(87, 121, 26, 255), speed);
			DrawVecField(context, "Z", vec3.z, &hasChanged, IM_COL32(43, 86, 138, 255), speed);
			ImGui::EndTable();
		}

		if (hasChanged)
		{
			ImGuiCommitFieldChanges(context, &vec3, sizeof(Vec3));
		}
	}

	void DrawQuatField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		static int rotationMode = 0;

		f32  speed = 0.005f;
		Quat quat = *static_cast<const Quat*>(value);

		if (rotationMode == 0)
		{
			Vec3 euler = Math::Degrees(Math::EulerAngles(quat));
			bool vecHasChanged = false;

			if (ImGui::BeginTable("##vec3-table", 3))
			{
				DrawVecField(context, "X", euler.x, &vecHasChanged, IM_COL32(138, 46, 61, 255), speed);
				DrawVecField(context, "Y", euler.y, &vecHasChanged, IM_COL32(87, 121, 26, 255), speed);
				DrawVecField(context, "Z", euler.z, &vecHasChanged, IM_COL32(43, 86, 138, 255), speed);
				ImGui::EndTable();
			}

			if (vecHasChanged)
			{
				Quat newValue = Quat{Math::Radians(euler)};
				ImGuiCommitFieldChanges(context, &newValue, sizeof(Quat));
			}
		}
		else
		{
			bool hasChanged = false;
			if (ImGui::BeginTable("##quat-table", 4))
			{
				DrawVecField(context, "X", quat.x, &hasChanged, IM_COL32(138, 46, 61, 255), speed);
				DrawVecField(context, "Y", quat.y, &hasChanged, IM_COL32(87, 121, 26, 255), speed);
				DrawVecField(context, "Z", quat.z, &hasChanged, IM_COL32(43, 86, 138, 255), speed);
				DrawVecField(context, "W", quat.w, &hasChanged, IM_COL32(84, 74, 119, 255), speed);
				ImGui::EndTable();
			}

			if (hasChanged)
			{
				ImGuiCommitFieldChanges(context, &quat, sizeof(Quat));
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

	void DrawColorField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		Color        color = *static_cast<const Color*>(value);
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

			ImGui::ColorPicker4("##picker", col, flags);

			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				Color::FromVec4(color, {col[0], col[1], col[2], col[3]});
				ImGuiCommitFieldChanges(context, &color, sizeof(Color));
			}

			ImGui::EndPopup();
		}
	}

	bool CanDrawEnumField(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.fieldProps.isEnum && check.reflectFieldType != nullptr;
	}

	void DrawEnumField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		char str[30];
		sprintf(str, "###enumid%llu", context.id);

		ImGui::SetNextItemWidth(-1);
		ReflectValue* reflectValue = context.reflectFieldType->FindValue(value);

		if (context.overriden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		if (ImGui::BeginCombo(str, reflectValue != nullptr ? reflectValue->GetDesc().CStr() : ""))
		{
			for (const auto& valueHandler : context.reflectFieldType->GetValues())
			{
				if (ImGui::Selectable(valueHandler->GetDesc().CStr()))
				{
					i64 code = valueHandler->GetCode();
					ImGuiCommitFieldChanges(context, &code, sizeof(i64));
				}
			}
			ImGui::EndCombo();
		}

		if (context.overriden)
		{
			ImGui::PopStyleColor();
		}
	}

	void DrawBoolField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		char str[30];
		sprintf(str, "###txtid%llu", context.id);

		bool boolValue = *static_cast<const bool*>(value);

		if (context.overriden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		if (ImGui::Checkbox(str, &boolValue))
		{
			ImGuiCommitFieldChanges(context, &boolValue, sizeof(bool));
		}

		if (context.overriden)
		{
			ImGui::PopStyleColor();
		}
	}

	void DrawFloatField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		bool f64Value = false;

		if (context.reflectField)
		{
			f64Value = context.reflectField->GetProps().typeId == TypeInfo<f64>::ID();
		}

		if (context.resourceField)
		{
			// ResourceObject is always 64bits
			f64Value = true;
		}


		char str[30];
		sprintf(str, "###%llu", context.id);

		usize size = f64Value ? sizeof(f64) : sizeof(f32);

		char buffer[sizeof(f64)];
		memcpy(buffer, value, size);

		float               step = 0.0f;
		float               stepFast = 0.0f;
		const char*         format = "%.3f";
		ImGuiInputTextFlags flags = 0;

		ImGui::SetNextItemWidth(-1);

		if (context.overriden)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		//		if (const UIFloatProperty* property = context.info.fieldHandler ? context.info.fieldHandler->GetAttribute<UIFloatProperty>() : nullptr)
		//		{
		//			SK_ASSERT(false, "not implemented yet");
		//
		//			// if (ImGui::SliderFloat(str, &floatValue, property->minValue, property->maxValue))
		//			// {
		//			//     //context->editingId = id;
		//			// }
		//
		//			// else if (context->editingId == id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		//			// {
		//			//     // context->editingId = U32_MAX;
		//			//     // if (hasChanged)
		//			//     // {
		//			//     //     *hasChanged = true;
		//			//     // }
		//			// }
		//		}

		ImGui::InputScalar(str, f64Value ? ImGuiDataType_Double : ImGuiDataType_Float, buffer, step > 0.0f ? &step : nullptr, stepFast > 0.0f ? &stepFast : nullptr, format, flags);

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			ImGuiCommitFieldChanges(context, buffer, size);
		}

		if (context.overriden)
		{
			ImGui::PopStyleColor();
		}
	}


	template <typename T>
	void DrawResource(const ImGuiDrawFieldContext& context, const RID rid, u64 id, TypeID typeId, T&& func)
	{
		static String stringCache = {};

		auto& style = ImGui::GetStyle();
		auto& io = ImGui::GetIO();

		bool isEntityDraw = typeId == TypeInfo<Entity>::ID();

		char pushStr[30];
		sprintf(pushStr, "###push%llu", id);

		bool openPopup = false;

		String name = ResourceAssets::GetAssetName(rid);

		ImGui::SetNextItemWidth(-22 * ImGui::GetStyle().ScaleFactor);
		ImGui::PushID(pushStr);

		{
			if (context.overriden)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
			}

			ImGuiInputTextReadOnly(id, name);

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered())
			{
				RID assetToSelect = {};
				if (isEntityDraw)
				{
					SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
					if (Resources::IsParentOf(sceneEditor->GetRootEntity(), rid))
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

			if (context.overriden)
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

		bool visible = true;

		char popupModalName[50] = {};
		sprintf(popupModalName, "Resource Selection###window%llu", id);

		if (openPopup)
		{
			visible = true;
			ImGui::OpenPopup(popupModalName);
			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(960 * ImGui::GetStyle().ScaleFactor, 540 * ImGui::GetStyle().ScaleFactor), ImGuiCond_Appearing);
		}


		auto originalPadding = ImGui::GetStyle().WindowPadding;
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (ImGui::BeginPopupModal(popupModalName, &visible))
		{
			{
				static String searchResourceString;

				ScopedStyleVar windowPadding2(ImGuiStyleVar_WindowPadding, originalPadding);
				ImGui::BeginChild(1000, ImVec2(0, (25 * style.ScaleFactor) + originalPadding.y), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

				ImGui::SetNextItemWidth(-1);
				ImGuiSearchInputText(12471247, searchResourceString);
				ImGui::EndChild();
			}

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + originalPadding.y);
			auto  p1 = ImGui::GetCursorScreenPos();
			auto  p2 = ImVec2(ImGui::GetContentRegionAvail().x + p1.x, p1.y);
			auto* drawList = ImGui::GetWindowDrawList();
			drawList->AddLine(p1, p2, ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Separator]), 1.f * style.ScaleFactor);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f * style.ScaleFactor);

			{
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleVar   windowPadding2(ImGuiStyleVar_WindowPadding, originalPadding);

				static f32 zoom = 1.0;

				ImGui::SetWindowFontScale(zoom);

				if (ImGui::BeginChild(10000, ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding))
				{
					if (!isEntityDraw)
					{
						if (ImGuiBeginContentTable("asset-selection", zoom))
						{
							{
								ImGuiContentItemDesc contentItem{};
								contentItem.id = HashValue("None-Id");
								contentItem.label = "None";
								contentItem.thumbnailScale = zoom;

								ImGuiContentItemState state = ImGuiContentItem(contentItem);

								if (state.enter)
								{
									ImGui::CloseCurrentPopup();
									func(RID{});
								}
							}

							for (RID resourceAsset : Resources::GetResourceByType(typeId))
							{
								if (Resources::HasValue(resourceAsset))
								{
									stringCache = ResourceAssets::GetAssetName(resourceAsset);

									ImGuiContentItemDesc contentItem{};
									contentItem.id = resourceAsset.id;
									contentItem.label =  stringCache.CStr();
									contentItem.thumbnailScale = zoom;
									//contentItem.texture = assetFile->GetThumbnail();

									ImGuiContentItemState state = ImGuiContentItem(contentItem);

									if (state.enter)
									{
										ImGui::CloseCurrentPopup();
										func(resourceAsset);
									}
								}
							}

							ImGuiEndContentTable();
						}
					}
					else
					{
						ScopedStyleColor childBg(ImGuiCol_FrameBg, IM_COL32(27, 28, 30, 255));
						if (ImGui::BeginListBox("Entities", ImVec2(-FLT_MIN, -FLT_MIN)))
						{
							SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();

							if (ImGui::Selectable(ICON_FA_CUBE " None", false, ImGuiSelectableFlags_AllowDoubleClick))
							{
								if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
								{
									ImGui::CloseCurrentPopup();
									func(RID{});
								}
							}

							std::function<void(RID entity)> drawEntity;
							drawEntity = [&](RID entity)
							{
								ResourceObject entityObject = Resources::Read(entity);

								stringCache.Clear();
								stringCache += ICON_FA_CUBE;
								stringCache += " ";
								stringCache += entityObject.GetString(EntityResource::Name);

								ImGui::PushID(IntToPtr(entity.id));

								if (ImGui::Selectable(stringCache.CStr(), false, ImGuiSelectableFlags_AllowDoubleClick))
								{
									if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
									{
										ImGui::CloseCurrentPopup();
										func(entity);
									}
								}

								ImGui::PopID();

								entityObject.IterateSubObjectList(EntityResource::Children, [&](RID child)
								{
									drawEntity(child);
								});

							};

							drawEntity(sceneEditor->GetRootEntity());

							ImGui::EndListBox();

						}
					}
					ImGui::EndChild();
					ImGui::SetWindowFontScale(1);
				}
			}

			if (ImGui::IsKeyDown(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool CanDrawResourceField(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.resourceFieldType == ResourceFieldType::Reference;
	}

	void DrawResourceField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		SK_ASSERT(context.resourceField, "reflectFieldType cannot be null");

		RID rid = *static_cast<const RID*>(value);
		DrawResource(context, rid, context.id, context.resourceField->GetSubType(), [&](RID rid)
		{
			ImGuiCommitFieldChanges(context, &rid, sizeof(RID));
		});
	}

	bool CanDrawArrayField(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.fieldProps.typeApi == TypeInfo<ArrayApi>::ID() && check.resourceFieldType != ResourceFieldType::ReferenceArray;
	}

	namespace
	{
		struct ArrayCustomContext
		{
			Array<ImGuiFieldRenderer> draws;
			VoidPtr instance;
			ReflectType* type;
		};
	}

	void ArrayDestroyCustomContext(VoidPtr context)
	{
		DestroyAndFree(static_cast<ArrayCustomContext*>(context));
	}

	VoidPtr ArrayCreateCustomContext(const ImGuiDrawFieldDrawCheck& drawCheck)
	{
		ArrayApi api = {};
		drawCheck.fieldProps.getTypeApi(&api);

		Array<ImGuiFieldRenderer> draws;

		ImGuiDrawFieldDrawCheck check;
		check.fieldProps = ToFieldProps(api.GetProps());
		check.reflectField = nullptr;
		check.reflectFieldType = Reflection::FindTypeById(check.fieldProps.typeId);

		if (drawCheck.resourceFieldType == ResourceFieldType::ReferenceArray)
		{
			check.resourceFieldType = ResourceFieldType::Reference;
		}

		for (ImGuiFieldRenderer fieldRenderer : ImGuiGetFieldRenders())
		{
			if (fieldRenderer.canDrawField(check))
			{
				draws.EmplaceBack(fieldRenderer);
			}
		}

		if (!draws.Empty())
		{
			return Alloc<ArrayCustomContext>(draws, api.Create(), check.reflectFieldType);
		}

		return nullptr;
	}

	void DrawArrayField(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
		if (!context.customContext) return;

		ArrayCustomContext& customContext = *static_cast<ArrayCustomContext*>(context.customContext);

		bool canAdd = true;
		bool canRemove = true;

		ArrayApi api;
		context.fieldProps.getTypeApi(&api);
		api.Copy(customContext.instance,  value);
		usize size = api.Size(customContext.instance);

		TypeProps arrProps = api.GetProps();

		if (context.reflectField != nullptr)
		{
			if (const UIArrayProperty* property = context.reflectField->GetAttribute<UIArrayProperty>())
			{
				canAdd = property->canAdd;
				canRemove = property->canRemove;
			}
		}

		ImGui::BeginDisabled(!canAdd);

		if (ImGui::Button(ICON_FA_PLUS))
		{
			api.PushNew(customContext.instance);
			ImGuiCommitFieldChanges(context, customContext.instance, arrProps.size);
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		ImGui::BeginDisabled(!canRemove || size == 0);

		if (ImGui::Button(ICON_FA_MINUS))
		{
			api.PopBack(customContext.instance);
			ImGuiCommitFieldChanges(context, customContext.instance, arrProps.size);
		}

		ImGui::EndDisabled();

		struct DrawFieldItemUserData
		{
			usize index;
			const ImGuiDrawFieldContext& originalContext;
			ArrayCustomContext& customContext;
			ArrayApi api;
		};

		ImGuiDrawFieldContext fieldContext;
		fieldContext.reflectFieldType = customContext.type;
		fieldContext.callback = [](const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size)
		{
			DrawFieldItemUserData& userData = *static_cast<DrawFieldItemUserData*>(context.userData);
			userData.api.Set(userData.customContext.instance, userData.index, pointer);

			TypeProps arrProps = userData.api.GetProps();
			ImGuiCommitFieldChanges(userData.originalContext, userData.customContext.instance, arrProps.size);
		};

		for (usize i = 0; i < size; ++i)
		{
			DrawFieldItemUserData userData{i, context, customContext, api};

			fieldContext.userData = &userData;
			fieldContext.id = context.id + i;

			ImGui::TableNextColumn();
			ImGui::TableNextColumn();

			for (ImGuiFieldRenderer draw : customContext.draws)
			{
				draw.drawField(fieldContext, api.Get(customContext.instance, i));
			}
		}
	}

	bool CanDrawReferenceArray(const ImGuiDrawFieldDrawCheck& check)
	{
		return check.resourceFieldType == ResourceFieldType::ReferenceArray;
	}

	void DrawReferenceArray(const ImGuiDrawFieldContext& context, ConstPtr value)
	{
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

	void RegisterFieldRenderers()
	{
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Vec2>, .drawField = DrawVec2Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Vec3>, .drawField = DrawVec3Field});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Quat>, .drawField = DrawQuatField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<Color>, .drawField = DrawColorField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<f32, f64>, .drawField = DrawFloatField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawField<bool>, .drawField = DrawBoolField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawEnumField, .drawField = DrawEnumField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawResourceField, .drawField = DrawResourceField});
		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{.canDrawField = CanDrawReferenceArray, .drawField = DrawReferenceArray});

		ImGuiRegisterFieldRenderer(ImGuiFieldRenderer{
			.canDrawField = CanDrawArrayField,
			.drawField = DrawArrayField,
			.createCustomContext = ArrayCreateCustomContext,
			.destroyCustomContext = ArrayDestroyCustomContext
		});
	}
}
