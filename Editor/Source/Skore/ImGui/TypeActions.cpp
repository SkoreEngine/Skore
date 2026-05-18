


#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Navigation/Components/NavMeshSurface.hpp"
#include "Skore/Navigation/Navigation.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Scene/SceneEditor.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"

namespace Skore
{

	static Logger& logger = Logger::GetLogger("Skore::TypeActions");

	static void BuildNavMesh(const ResourceObject& object)
	{
		Scene* scene = Editor::GetActiveWorkspace()->GetSceneEditor()->GetCurrentScene();
		if (!scene) return;

		NavMeshBuildSettings settings;
		settings.cellSize             = static_cast<f32>(object.GetFloat(object.GetIndex("cellSize")));
		settings.cellHeight           = static_cast<f32>(object.GetFloat(object.GetIndex("cellHeight")));
		settings.agentHeight          = static_cast<f32>(object.GetFloat(object.GetIndex("agentHeight")));
		settings.agentRadius          = static_cast<f32>(object.GetFloat(object.GetIndex("agentRadius")));
		settings.agentMaxClimb        = static_cast<f32>(object.GetFloat(object.GetIndex("agentMaxClimb")));
		settings.agentMaxSlope        = static_cast<f32>(object.GetFloat(object.GetIndex("agentMaxSlope")));
		settings.regionMinSize        = static_cast<f32>(object.GetFloat(object.GetIndex("regionMinSize")));
		settings.regionMergeSize      = static_cast<f32>(object.GetFloat(object.GetIndex("regionMergeSize")));
		settings.edgeMaxLen           = static_cast<f32>(object.GetFloat(object.GetIndex("edgeMaxLen")));
		settings.edgeMaxError         = static_cast<f32>(object.GetFloat(object.GetIndex("edgeMaxError")));
		settings.vertsPerPoly         = static_cast<i32>(object.GetInt(object.GetIndex("vertsPerPoly")));
		settings.detailSampleDist     = static_cast<f32>(object.GetFloat(object.GetIndex("detailSampleDist")));
		settings.detailSampleMaxError = static_cast<f32>(object.GetFloat(object.GetIndex("detailSampleMaxError")));

		NavigationScene navigationScene;
		navigationScene.BuildNavMesh(scene, settings);

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Build NavMesh");

		RID navMeshData = Resources::Create<NavMeshResource>(UUID::RandomUUID(), scope);

		ResourceBuffer resourceBuffer = ResourceAssets::CreateTempBuffer();
		FileHandler bufferFile = resourceBuffer.OpenFile(AccessMode::WriteOnly);
		FileSystem::WriteFile(bufferFile, navigationScene.GetNavData(), navigationScene.GetNavDataSize());
		FileSystem::CloseFile(bufferFile);

		ResourceObject navMeshObject = Resources::Write(navMeshData);
		navMeshObject.SetBuffer(NavMeshResource::NavData, resourceBuffer);
		navMeshObject.Commit(scope);

		ResourceObject write = Resources::Write(object.GetRID());
		write.SetSubObject(object.GetIndex("navMeshData"), navMeshData);
		write.Commit(scope);
	}

	static void DrawAvatarBoneTree(RID boneRID, UndoRedoScope*& scope)
	{
		ResourceObject boneObject = Resources::Read(boneRID);
		if (!boneObject) return;

		String boneName = boneObject.GetString(AnimationAvatarBoneResource::BoneName);
		bool enabled = boneObject.GetBool(AnimationAvatarBoneResource::Enabled);
		auto children = boneObject.GetSubObjectList(AnimationAvatarBoneResource::Children);
		bool hasChildren = boneObject.GetSubObjectListCount(AnimationAvatarBoneResource::Children) > 0;

		ImGui::PushID(boneRID.id);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		f32 columnWidth = ImGui::GetColumnWidth();
		f32 checkboxWidth = ImGui::GetFrameHeight();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (columnWidth - checkboxWidth) * 0.5f);

		if (ImGui::Checkbox("##enabled", &enabled))
		{
			if (!scope)
			{
				scope = Editor::CreateUndoRedoScope("Edit Avatar Bone");
			}
			ResourceObject boneWrite = Resources::Write(boneRID);
			boneWrite.SetBool(AnimationAvatarBoneResource::Enabled, enabled);
			boneWrite.Commit(scope);
		}

		ImGui::TableNextColumn();

		ImGui::SetNextItemOpen(false, ImGuiCond_Once);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
		if (!hasChildren)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}

		bool nodeOpen = ImGui::TreeNodeEx("##bone", flags);
		ImGui::SameLine();
		ImGui::TextUnformatted(boneName.CStr());

		if (hasChildren && nodeOpen)
		{
			for (RID childRID : children)
			{
				DrawAvatarBoneTree(childRID, scope);
			}
			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	static void DrawAnimationAvatarResource(const ResourceObject& object)
	{

		RID parentRID = Resources::GetParent(object.GetRID());

		RID previewEntity{};

		if (parentRID && Resources::GetType(parentRID)->GetID() == sktypeid(AnimationControllerResource))
		{
			if (ResourceObject parentObject = Resources::Read(parentRID))
			{
				previewEntity = parentObject.GetReference(AnimationControllerResource::PreviewEntity);
			}
		}
		else
		{
			ScopedStyleColor color(ImGuiCol_Text, ImVec4(0.95f, 0.05f, 0.05f, 1.0f));
			ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " Parent must be an AnimationControllerResource ");
		}

		if (!previewEntity)
		{
			ScopedStyleColor color(ImGuiCol_Text, ImVec4(0.95f, 0.05f, 0.05f, 1.0f));
			ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " Preview Entity on AnimationController must be provided ");
		}

		ImGui::BeginHorizontal(object.GetRID().id, ImVec2(ImGui::GetContentRegionAvail().x, 0));
		ImGui::Spring(1.0);
		ImGui::BeginDisabled(!previewEntity);

		if (ImGui::Button("Build From Preview Entity"))
		{
			if (ResourceObject entityObject = Resources::Read(previewEntity))
			{
				//TODO - loop needs to break when find first Skeleton.
				entityObject.IterateAllSubObjects([&](u32 index, RID subRID)
				{
					if (Resources::GetType(subRID)->GetID() == sktypeid(Skeleton))
					{
						if (ResourceObject skeletonObject = Resources::Read(subRID))
						{
							struct BoneItem
							{
								String     name;
								u32        parentIndex;
								Array<u32> children;
								RID        avatarBone;
							};

							Array<BoneItem> bones;
							bones.Reserve(skeletonObject.GetSubObjectListCount(skeletonObject.GetIndex("bones")));

							for (RID boneRID : skeletonObject.GetSubObjectList(skeletonObject.GetIndex("bones")))
							{
								ResourceObject boneObject = Resources::Read(boneRID);
								bones.EmplaceBack(BoneItem{
									boneObject.GetString(boneObject.GetIndex("name")),
									static_cast<u32>(boneObject.GetUInt(boneObject.GetIndex("parentIndex")))
								});
							}

							for (int i = 0; i < bones.Size(); ++i)
							{
								BoneItem& bone = bones[i];
								bone.parentIndex < bones.Size() ? bones[bone.parentIndex].children.EmplaceBack(i) : 0;
							}

							UndoRedoScope* scope = Editor::CreateUndoRedoScope("Build Avatar");

							std::function<RID(BoneItem&)> buildAvatarBone;
							buildAvatarBone = [&](BoneItem& item)
							{
								logger.Info("Building Avatar Bone: {}", item.name);
								RID avatarBone = Resources::Create<AnimationAvatarBoneResource>(UUID::RandomUUID(), scope);
								ResourceObject avatarBoneObject = Resources::Write(avatarBone);
								avatarBoneObject.SetString(AnimationAvatarBoneResource::BoneName, item.name);
								avatarBoneObject.SetBool(AnimationAvatarBoneResource::Enabled, true);

								for (auto& child : item.children)
								{
									avatarBoneObject.AddToSubObjectList(AnimationAvatarBoneResource::Children, buildAvatarBone(bones[child]));
								}

								avatarBoneObject.Commit(scope);
								return avatarBone;
							};

							RID root = buildAvatarBone(bones[0]);

							ResourceObject avatarResourceObject = Resources::Write(object.GetRID());
							avatarResourceObject.SetSubObject(AnimationAvatarResource::RootBone, root);
							avatarResourceObject.Commit(scope);
						}
					}
				});
			}
		}

		ImGui::EndDisabled();

		ImGui::Spring(1.0);
		ImGui::EndHorizontal();

		RID rootBone = object.GetSubObject(AnimationAvatarResource::RootBone);
		if (rootBone)
		{
			ImGui::Separator();

			if (ImGui::BeginTable("AvatarBones", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_IndentDisable, 30.0f * ImGui::GetStyle().ScaleFactor);
				ImGui::TableSetupColumn("Bone", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
				ImGui::TableHeadersRow();

				UndoRedoScope* scope = nullptr;
				DrawAvatarBoneTree(rootBone, scope);

				ImGui::EndTable();
			}
		}
	}

	void RegisterTypeActions()
	{
		ImGuiRegisterResourceTypeAction(sktypeid(NavMeshSurface), "Build NavMesh", &BuildNavMesh);
		ImGuiRegisterResourceDrawCallback(sktypeid(AnimationAvatarResource), &DrawAnimationAvatarResource);
	}
}
