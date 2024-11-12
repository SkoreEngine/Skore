#include "SceneEditor.hpp"

#include "Fyrion/Core/Logger.hpp"
#include "Fyrion/Core/StringUtils.hpp"
#include "Fyrion/Editor/Editor.hpp"
#include "Fyrion/Editor/Asset/AssetEditor.hpp"
#include "Fyrion/Editor/ImGui/ImGuiEditor.hpp"
#include "Fyrion/Scene/Component/Component.hpp"
#include "Fyrion/Scene/Component/TransformComponent.hpp"

namespace Fyrion
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Fyrion::Scene");

        struct TransformUpdateAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            SceneEditor& sceneEditor;
            UUID         gameObjectId;
            UUID         transformUUID;
            UUID         instanceUUID;
            Transform    oldTransform;
            Transform    newTransform;

            TransformUpdateAction(SceneEditor& sceneEditor, const UUID& gameObjectId, TransformComponent* transformComponent, const Transform& oldTransform, const Transform& newTransform)
                : sceneEditor(sceneEditor),
                  gameObjectId(gameObjectId),
                  transformUUID(transformComponent->uuid),
                  instanceUUID(transformComponent->instance != nullptr ? transformComponent->instance->uuid : UUID{}),
                  oldTransform(oldTransform),
                  newTransform(newTransform) {}


            TransformComponent* GetComponent()
            {
                if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(gameObjectId))
                {
                    if (TransformComponent* transformComponent = static_cast<TransformComponent*>(object->FindComponentByUUID(transformUUID)))
                    {
                        return transformComponent;
                    }

                    if (instanceUUID)
                    {
                        if (TransformComponent* transformComponent = static_cast<TransformComponent*>(object->FindComponentByInstance(instanceUUID)))
                        {
                            return transformComponent;
                        }
                    }
                }
                return nullptr;
            }

            void Commit() override
            {
                if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(gameObjectId))
                {
                    if (TransformComponent* transformComponent = GetComponent())
                    {
                        transformComponent->SetTransform(newTransform);
                        object->AddComponentOverride(transformComponent);
                    }
                    sceneEditor.MarkDirty();
                }
            }

            void Rollback() override
            {
                if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(gameObjectId))
                {
                    if (TransformComponent* transformComponent = GetComponent())
                    {
                        transformComponent->SetTransform(oldTransform);
                        object->AddComponentOverride(transformComponent);
                    }
                    sceneEditor.MarkDirty();
                }
            }
        };

        struct RenameAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            SceneEditor& sceneEditor;
            UUID         gameObjectId;
            String       newName;
            String       oldName;

            RenameAction(SceneEditor& sceneEditor, const UUID& gameObjectId, const String& newName, const String& oldName)
                : sceneEditor(sceneEditor),
                  gameObjectId(gameObjectId),
                  newName(newName),
                  oldName(oldName) {}


            void Commit() override
            {
                if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(gameObjectId))
                {
                    object->SetName(newName);
                    sceneEditor.MarkDirty();
                }
            }

            void Rollback() override
            {
                if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(gameObjectId))
                {
                    object->SetName(oldName);
                    sceneEditor.MarkDirty();
                }
            }
        };

        struct CreateGameObjectAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            struct NewGameObject
            {
                UUID uuid;
                UUID parent;
            };

            SceneEditor&         sceneEditor;
            UUID                 prefab;
            bool                 childrenOfSelected;
            Array<NewGameObject> newObjects;

            CreateGameObjectAction(SceneEditor& sceneEditor, UUID prefab, bool childrenOfSelected)
                : sceneEditor(sceneEditor),
                  prefab(prefab),
                  childrenOfSelected(childrenOfSelected)
            {
                if (childrenOfSelected && !sceneEditor.GetSelectedObjects().Empty())
                {
                    newObjects.Reserve(sceneEditor.GetSelectedObjects().Size());
                    for (auto& it : sceneEditor.GetSelectedObjects())
                    {
                        newObjects.EmplaceBack(UUID::RandomUUID(), it.first->GetUUID());
                    }
                }
                else
                {
                    newObjects.EmplaceBack(UUID::RandomUUID(), sceneEditor.GetScene()->GetRootObject().GetUUID());
                }
            }

            void Commit() override
            {
                sceneEditor.ClearSelection();
                for (NewGameObject& newGameObject : newObjects)
                {
                    if (GameObject* parent = sceneEditor.GetScene()->FindObjectByUUID(newGameObject.parent))
                    {
                        GameObject* child = parent->Create(newGameObject.uuid);
                        child->SetPrefab(prefab);
                        child->SetName(SceneEditor::GetUniqueObjectName(*child));
                        sceneEditor.SelectObject(*child);
                    }
                }
                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                for (NewGameObject& newGameObject : newObjects)
                {
                    if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(newGameObject.uuid))
                    {
                        sceneEditor.DeselectObject(*object);
                        object->Destroy();
                    }
                }
                sceneEditor.MarkDirty();
            }

        };

        struct DestroyObjectAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            SceneEditor& sceneEditor;
            String json;
            Array<UUID> selectedObjects;

            DestroyObjectAction(SceneEditor& sceneEditor) : sceneEditor(sceneEditor)
            {
                JsonArchiveWriter writer;

                ArchiveValue arr = writer.CreateArray();
                for (auto& it : sceneEditor.GetSelectedObjects())
                {
                    selectedObjects.EmplaceBack(it.first->GetUUID());
                    ArchiveValue obj = it.first->Serialize(writer);
                    writer.AddToObject(obj, "_parent", writer.StringValue(it.first->GetParent()->GetUUID().ToString()));
                    writer.AddToArray(arr, obj);
                }

                json = JsonArchiveWriter::Stringify(arr, false, true);
            }

            void Commit() override
            {
                for (auto& uuid : selectedObjects)
                {
                    if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(uuid))
                    {
                        if (object->GetParent() != nullptr)
                        {
                            object->GetParent()->RemovePrefabObject(object);
                        }
                        object->Destroy();
                    }
                }
                sceneEditor.ClearSelection();
                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                JsonArchiveReader reader(json, true);
                ArchiveValue      arr = reader.GetRoot();

                usize        size = reader.ArraySize(arr);
                ArchiveValue item{};

                for (int i = 0; i < size; ++i)
                {
                    item = reader.ArrayNext(arr, item);
                    UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "uuid")));
                    UUID parentUUID = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "_parent")));

                    if (GameObject* parent = sceneEditor.GetScene()->FindObjectByUUID(parentUUID))
                    {
                        reader.GetObjectValue(item, "object");
                        GameObject* obj = parent->Create(uuid);
                        obj->Deserialize(reader, item);
                    }
                }

                sceneEditor.MarkDirty();
            }
        };

        struct DuplicateObjectAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);
            SceneEditor& sceneEditor;

            String json;
            Array<UUID> newObjects;

            explicit DuplicateObjectAction(SceneEditor& sceneEditor)
                : sceneEditor(sceneEditor)
            {
                JsonArchiveWriter writer;
                ArchiveValue arr = writer.CreateArray();

                for (auto& it : sceneEditor.GetSelectedObjects())
                {
                    GameObject* newObject = it.first->Duplicate(nullptr);
                    newObject->SetName(SceneEditor::GetUniqueObjectName(*newObject, it.first->GetParent()));

                    ArchiveValue obj = newObject->Serialize(writer);
                    writer.AddToObject(obj, "_parent", writer.StringValue(it.first->GetParent()->GetUUID().ToString()));
                    writer.AddToArray(arr, obj);

                    newObjects.EmplaceBack(newObject->GetUUID());

                }
                json = JsonArchiveWriter::Stringify(arr, false, true);
            }

            void Commit() override
            {
                sceneEditor.ClearSelection();

                JsonArchiveReader reader(json, true);
                ArchiveValue      arr = reader.GetRoot();

                usize size = reader.ArraySize(arr);
                ArchiveValue item{};

                for (int i = 0; i < size; ++i)
                {
                    item = reader.ArrayNext(arr, item);
                    UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "uuid")));
                    UUID parentUUID = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "_parent")));

                    if (GameObject* parent = sceneEditor.GetScene()->FindObjectByUUID(parentUUID))
                    {
                        reader.GetObjectValue(item, "object");
                        GameObject* obj = parent->Create(uuid);
                        obj->Deserialize(reader, item);
                        sceneEditor.SelectObject(*obj);
                    }
                }

                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                for (UUID& uuid : newObjects)
                {
                    if (GameObject* object = sceneEditor.GetScene()->FindObjectByUUID(uuid))
                    {
                        sceneEditor.DeselectObject(*object);
                        object->Destroy();
                    }
                }
                sceneEditor.MarkDirty();
            }
        };
    }

    Scene* SceneEditor::GetScene() const
    {
        return scene;
    }

    AssetFile* SceneEditor::GetAssetFile() const
    {
        return assetFile;
    }

    void SceneEditor::SetScene(AssetFile* assetFile)
    {
        this->assetFile = assetFile;

        scene = Assets::Load<Scene>(assetFile->uuid);
        if (scene)
        {
            scene->Start();
        }
    }

    void SceneEditor::ClearSelection()
    {
        for (const auto& it : selectedObjects)
        {
            onGameObjectDeselectionHandler.Invoke(it.first);
        }
        selectedObjects.Clear();
    }

    void SceneEditor::SelectObject(GameObject& object)
    {
        selectedObjects.Emplace(&object);
        onGameObjectSelectionHandler.Invoke(&object);
    }

    void SceneEditor::DeselectObject(GameObject& object)
    {
        onGameObjectDeselectionHandler.Invoke(&object);
        selectedObjects.Erase(&object);
    }

    bool SceneEditor::IsSelected(GameObject& object) const
    {
        return selectedObjects.Has(&object);
    }

    bool SceneEditor::IsParentOfSelected(GameObject& object) const
    {
        for (const auto& it : selectedObjects)
        {
            GameObject* parent = it.first->GetParent();
            if (parent)
            {
                if (parent == &object)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void SceneEditor::RenameObject(GameObject& object, StringView newName)
    {
        Editor::CreateTransaction()->CreateAction<RenameAction>(*this, object.GetUUID(), newName, object.GetName())->Commit();
    }

    void SceneEditor::DestroySelectedObjects()
    {
        Editor::CreateTransaction()->CreateAction<DestroyObjectAction>(*this)->Commit();
    }

    void SceneEditor::CreateGameObject(UUID prefab, bool checkSelected)
    {
        if (scene == nullptr) return;
        Editor::CreateTransaction()->CreateAction<CreateGameObjectAction>(*this, prefab, checkSelected)->Commit();
    }

    void SceneEditor::DuplicateSelected()
    {
        if (scene == nullptr) return;
        if (!selectedObjects.Empty())
        {
            Editor::CreateTransaction()->CreateAction<DuplicateObjectAction>(*this)->Commit();
        }
    }

    bool SceneEditor::IsValidSelection()
    {
        return scene != nullptr;
    }

    void SceneEditor::AddComponent(GameObject* gameObject, TypeHandler* typeHandler)
    {
        if (const ComponentDesc* componentDesc = typeHandler->GetAttribute<ComponentDesc>())
        {
            if (!componentDesc->allowMultiple)
            {
                if (gameObject->GetComponent(typeHandler->GetTypeInfo().typeId))
                {
                    logger.Warn("multiple components of type {} are not allowed", typeHandler->GetName());
                    return;
                }
            }

            for (TypeID dependency : componentDesc->dependencies)
            {
                gameObject->GetOrAddComponent(dependency);
            }
        }

        gameObject->AddComponent(typeHandler, UUID::RandomUUID());
        assetFile->currentVersion++;
    }

    void SceneEditor::ResetComponent(GameObject* gameObject, Component* component)
    {
        gameObject->AddComponentOverride(component);
        assetFile->currentVersion++;
    }

    void SceneEditor::RemoveComponent(GameObject* gameObject, Component* component)
    {
        for (Component* otherComps : gameObject->GetComponents())
        {
            if (otherComps == component) continue;

            if (const ComponentDesc* componentDesc = otherComps->typeHandler->GetAttribute<ComponentDesc>())
            {
                for (TypeID dependency : componentDesc->dependencies)
                {
                    if (dependency == component->typeHandler->GetTypeInfo().typeId)
                    {
                        gameObject->RemovePrefabComponent(otherComps);
                        gameObject->RemoveComponent(otherComps);
                    }
                }
            }
        }

        gameObject->RemovePrefabComponent(component);
        gameObject->RemoveComponent(component);

        assetFile->currentVersion++;
    }

    void SceneEditor::UpdateComponent(GameObject* gameObject, Component* instance)
    {
        instance->OnChange();
        ImGui::ClearDrawData(instance);
        assetFile->currentVersion++;
    }

    void SceneEditor::UpdateTransform(GameObject* gameObject, const Transform& oldTransform, TransformComponent* transformComponent)
    {
        EditorTransaction* newTransaction = Editor::CreateTransaction();
        newTransaction->CreateAction<TransformUpdateAction>(*this, gameObject->GetUUID(), transformComponent, oldTransform, transformComponent->GetTransform())->Commit();
    }

    void SceneEditor::RemoveComponentOverride(GameObject* gameObject, Component* component)
    {
        gameObject->RemoveComponentOverride(component);
        assetFile->currentVersion++;
    }

    void SceneEditor::MarkDirty()
    {
        assetFile->currentVersion++;
    }

    HashSet<GameObject*>& SceneEditor::GetSelectedObjects()
    {
        return selectedObjects;
    }

    bool SceneEditor::IsSimulating() const
    {
        return false;
    }

    void SceneEditor::StartSimulation()
    {
        //TODO
    }

    void SceneEditor::StopSimulation()
    {
        //TODO
    }

    void SceneEditor::DoUpdate()
    {
        if (scene)
        {
            scene->FlushQueues();
        }
    }

    String SceneEditor::GetUniqueObjectName(GameObject& object, GameObject* parent)
    {
        GameObject* parentToUse = parent != nullptr ? parent : object.GetParent();

        String desiredName = object.GetPrefab() != nullptr ? object.GetPrefab()->GetName() : "Object";
        String finalName = desiredName;

        if (parentToUse != nullptr)
        {
            u32  count{};
            bool nameFound;
            do
            {
                nameFound = true;
                for (GameObject* child : parentToUse->GetChildren())
                {
                    if (child != &object && finalName == child->GetName())
                    {
                        finalName = desiredName;
                        finalName += " (";
                        finalName.Append(ToString(++count));
                        finalName += ")";
                        nameFound = false;
                        break;
                    }
                }
            }
            while (!nameFound);
        }
        return finalName;
    }

    void RegistrySceneEditorTypes()
    {
        Registry::Type<TransformUpdateAction>();
        Registry::Type<RenameAction>();
        Registry::Type<DestroyObjectAction>();
        Registry::Type<CreateGameObjectAction>();
        Registry::Type<DuplicateObjectAction>();
    }
}
