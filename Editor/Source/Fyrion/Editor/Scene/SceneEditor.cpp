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
                if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(gameObjectId))
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
                if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(gameObjectId))
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
                if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(gameObjectId))
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
                if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(gameObjectId))
                {
                    object->SetName(newName);
                    sceneEditor.MarkDirty();
                }
            }

            void Rollback() override
            {
                if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(gameObjectId))
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
            Array<UUID>          selectedObjects;

            CreateGameObjectAction(SceneEditor& sceneEditor, UUID prefab, bool childrenOfSelected)
                : sceneEditor(sceneEditor),
                  prefab(prefab),
                  childrenOfSelected(childrenOfSelected)
            {
                if (childrenOfSelected && !sceneEditor.selectedObjects.Empty())
                {
                    newObjects.Reserve(sceneEditor.selectedObjects.Size());
                    for (auto& it : sceneEditor.selectedObjects)
                    {
                        newObjects.EmplaceBack(UUID::RandomUUID(), it.first);
                    }
                }
                else
                {
                    newObjects.EmplaceBack(UUID::RandomUUID(), sceneEditor.GetActiveScene()->GetRootObject().GetUUID());
                }

                selectedObjects = sceneEditor.GetSelectObjectUUIDS();
            }

            void Commit() override
            {
                sceneEditor.ClearSelectionNoHistory();

                for (NewGameObject& newGameObject : newObjects)
                {
                    if (GameObject* parent = sceneEditor.GetActiveScene()->FindObjectByUUID(newGameObject.parent))
                    {
                        GameObject* child = parent->Create(newGameObject.uuid);
                        child->SetPrefab(prefab);
                        child->SetName(SceneEditor::GetUniqueObjectName(*child));
                        sceneEditor.SelectObjectNoHistory(*child);
                    }
                }
                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                for (NewGameObject& newGameObject : newObjects)
                {
                    if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(newGameObject.uuid))
                    {
                        sceneEditor.DeselectObjectNoHistory(*object);
                        object->Destroy();
                    }
                }

                sceneEditor.SelectObjectsNoHistory(selectedObjects);
                sceneEditor.MarkDirty();
            }
        };

        struct DestroyObjectAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            SceneEditor& sceneEditor;
            String       json;
            Array<UUID>  selectedObjects;

            DestroyObjectAction(SceneEditor& sceneEditor) : sceneEditor(sceneEditor)
            {
                selectedObjects = sceneEditor.GetSelectObjectUUIDS();

                JsonArchiveWriter writer;

                ArchiveValue arr = writer.CreateArray();
                for (auto& it : sceneEditor.selectedObjects)
                {
                    if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(it.first))
                    {
                        ArchiveValue obj = gameObject->Serialize(writer);
                        writer.AddToObject(obj, "_parent", writer.StringValue(gameObject->GetParent()->GetUUID().ToString()));
                        writer.AddToArray(arr, obj);
                    }
                }
                json = JsonArchiveWriter::Stringify(arr, false, true);
            }

            void Commit() override
            {
                for (auto& uuid : selectedObjects)
                {
                    if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(uuid))
                    {
                        if (object->GetParent() != nullptr)
                        {
                            object->GetParent()->RemovePrefabObject(object);
                        }
                        object->Destroy();
                    }
                }
                sceneEditor.ClearSelectionNoHistory();
                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                sceneEditor.ClearSelectionNoHistory();

                JsonArchiveReader reader(json, true);
                ArchiveValue      arr = reader.GetRoot();

                usize        size = reader.ArraySize(arr);
                ArchiveValue item{};

                for (int i = 0; i < size; ++i)
                {
                    item = reader.ArrayNext(arr, item);
                    UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "uuid")));
                    UUID parentUUID = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "_parent")));

                    if (GameObject* parent = sceneEditor.GetActiveScene()->FindObjectByUUID(parentUUID))
                    {
                        reader.GetObjectValue(item, "object");
                        GameObject* obj = parent->Create(uuid);
                        obj->Deserialize(reader, item);
                        sceneEditor.SelectObjectNoHistory(*obj);
                    }
                }

                sceneEditor.MarkDirty();
            }
        };

        struct DuplicateObjectAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);
            SceneEditor& sceneEditor;

            String      json;
            Array<UUID> newObjects;
            Array<UUID> selectedObejcts;

            explicit DuplicateObjectAction(SceneEditor& sceneEditor)
                : sceneEditor(sceneEditor)
            {
                selectedObejcts = sceneEditor.GetSelectObjectUUIDS();

                JsonArchiveWriter writer;
                ArchiveValue      arr = writer.CreateArray();

                for (auto& it : sceneEditor.selectedObjects)
                {
                    if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(it.first))
                    {
                        GameObject* newObject = object->Duplicate(nullptr);
                        newObject->SetName(SceneEditor::GetUniqueObjectName(*newObject, object->GetParent()));

                        ArchiveValue obj = newObject->Serialize(writer);
                        writer.AddToObject(obj, "_parent", writer.StringValue(object->GetParent()->GetUUID().ToString()));
                        writer.AddToArray(arr, obj);

                        newObjects.EmplaceBack(newObject->GetUUID());
                    }
                }
                json = JsonArchiveWriter::Stringify(arr, false, true);
            }

            void Commit() override
            {
                sceneEditor.ClearSelectionNoHistory();

                JsonArchiveReader reader(json, true);
                ArchiveValue      arr = reader.GetRoot();

                usize        size = reader.ArraySize(arr);
                ArchiveValue item{};

                for (int i = 0; i < size; ++i)
                {
                    item = reader.ArrayNext(arr, item);
                    UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "uuid")));
                    UUID parentUUID = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "_parent")));

                    if (GameObject* parent = sceneEditor.GetActiveScene()->FindObjectByUUID(parentUUID))
                    {
                        reader.GetObjectValue(item, "object");
                        GameObject* obj = parent->Create(uuid);
                        obj->Deserialize(reader, item);
                        sceneEditor.SelectObjectNoHistory(*obj);
                    }
                }

                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                sceneEditor.ClearSelectionNoHistory();
                for (UUID& uuid : newObjects)
                {
                    if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(uuid))
                    {
                        object->Destroy();
                    }
                }
                sceneEditor.SelectObjectsNoHistory(selectedObejcts);
                sceneEditor.MarkDirty();
            }
        };


        struct ObjectSelectionAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            enum class Type
            {
                Select,
                Deselect,
                ClearSelection
            };

            SceneEditor& sceneEditor;
            Type         selectionType;
            UUID         objectId;
            Array<UUID>  selectedObjects;

            ObjectSelectionAction(SceneEditor& sceneEditor, Type selectionType, const UUID& objectId)
                : sceneEditor(sceneEditor),
                  selectionType(selectionType),
                  objectId(objectId)
            {
                if (selectionType == Type::ClearSelection)
                {
                    for (const auto& it : sceneEditor.selectedObjects)
                    {
                        selectedObjects.EmplaceBack(it.first);
                    }
                }
            }


            void Commit() override
            {
                switch (selectionType)
                {
                    case Type::Select:
                    {
                        if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(objectId))
                        {
                            sceneEditor.selectedObjects.Emplace(gameObject->GetUUID());
                            sceneEditor.onGameObjectSelectionHandler.Invoke(gameObject->GetUUID());
                        }
                        break;
                    }
                    case Type::Deselect:
                        if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(objectId))
                        {
                            sceneEditor.selectedObjects.Erase(objectId);
                            sceneEditor.onGameObjectDeselectionHandler.Invoke(gameObject->GetUUID());
                        }
                        break;
                    case Type::ClearSelection:
                    {
                        for (UUID& id : selectedObjects)
                        {
                            if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(id))
                            {
                                sceneEditor.onGameObjectDeselectionHandler.Invoke(gameObject->GetUUID());
                            }
                        }
                        sceneEditor.selectedObjects.Clear();
                    }
                    break;
                }
            }

            void Rollback() override
            {
                switch (selectionType)
                {
                    case Type::Select:
                    {
                        if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(objectId))
                        {
                            sceneEditor.selectedObjects.Erase(objectId);
                            sceneEditor.onGameObjectDeselectionHandler.Invoke(gameObject->GetUUID());
                        }
                        break;
                    }
                    case Type::Deselect:
                        if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(objectId))
                        {
                            sceneEditor.selectedObjects.Emplace(gameObject->GetUUID());
                            sceneEditor.onGameObjectSelectionHandler.Invoke(gameObject->GetUUID());
                        }
                        break;
                    case Type::ClearSelection:
                    {
                        sceneEditor.selectedObjects.Clear();
                        for (UUID& id : selectedObjects)
                        {
                            if (GameObject* gameObject = sceneEditor.GetActiveScene()->FindObjectByUUID(id))
                            {
                                sceneEditor.selectedObjects.Insert(gameObject->GetUUID());
                                sceneEditor.onGameObjectSelectionHandler.Invoke(gameObject->GetUUID());
                            }
                        }
                    }
                    break;
                }
            }
        };

        struct ChangeParentAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            SceneEditor& sceneEditor;
            GameObject*  parent;

            struct ChangeParentData
            {
                GameObject* object;
                GameObject* oldParent;
                usize       currentIndex;
            };

            Array<ChangeParentData> sorted;

            ChangeParentAction(SceneEditor& sceneEditor, GameObject* parent, Span<GameObject*> objects)
                : sceneEditor(sceneEditor), parent(parent)
            {
                for (GameObject* object : objects)
                {
                    sorted.EmplaceBack(ChangeParentData{
                        .object = object,
                        .oldParent = object->GetParent(),
                        .currentIndex = object->GetIndex()
                    });
                }

                Sort(sorted.begin(), sorted.end(), [](const ChangeParentData& l, const ChangeParentData& r)
                {
                    return l.currentIndex < r.currentIndex;
                });
            }

            void Commit() override
            {
                for (auto& data : sorted)
                {
                    data.object->SetParent(parent);
                }
                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                for (auto& data : sorted)
                {
                    data.object->SetParent(data.oldParent);
                    data.object->MoveTo(data.currentIndex);
                }
                sceneEditor.MarkDirty();
            }
        };

        struct MoveObjectsAction : EditorAction
        {
            FY_BASE_TYPES(EditorAction);

            SceneEditor& sceneEditor;
            GameObject*  parent;
            usize        index;

            struct MoveObjectsData
            {
                GameObject* object;
                GameObject* oldParent;
                usize       currentIndex;
            };

            Array<MoveObjectsData> sorted;

            MoveObjectsAction(SceneEditor& sceneEditor, GameObject* parent, usize index, Span<GameObject*> objects)
                : sceneEditor(sceneEditor), parent(parent), index(index)
            {
                for (GameObject* object : objects)
                {
                    sorted.EmplaceBack(MoveObjectsData{
                        .object = object,
                        .oldParent = object->GetParent(),
                        .currentIndex = object->GetIndex()
                    });
                }

                Sort(sorted.begin(), sorted.end(), [](const MoveObjectsData& l, const MoveObjectsData& r)
                {
                    return l.currentIndex < r.currentIndex;
                });
            }

            void Commit() override
            {
                auto toIndex = index;

                for (auto& data : sorted)
                {
                    if (data.object->GetParent() != parent)
                    {
                        data.object->SetParent(parent);
                    }
                    bool curIndexHigh = data.currentIndex > toIndex;

                    data.object->MoveTo(toIndex);

                    if (curIndexHigh)
                    {
                        toIndex++;
                    }
                }
                sceneEditor.MarkDirty();
            }

            void Rollback() override
            {
                for (auto& data : sorted)
                {
                    if (data.oldParent != parent)
                    {
                        data.object->SetParent(data.oldParent);
                    }
                    data.object->MoveTo(data.currentIndex);
                }
                sceneEditor.MarkDirty();
            }
        };
    }

    SceneEditor::~SceneEditor()
    {
        if (simulationScene)
        {
            DestroyAndFree(simulationScene);
            simulationScene = nullptr;
        }
    }

    Scene* SceneEditor::GetActiveScene() const
    {
        return simulationScene ? simulationScene : editorScene;
    }

    AssetFile* SceneEditor::GetAssetFile() const
    {
        return assetFile;
    }

    void SceneEditor::SetScene(AssetFile* assetFile)
    {
        this->assetFile = assetFile;
        this->editorScene = Assets::Load<Scene>(assetFile->uuid);

        if (this->editorScene)
        {
            this->editorScene->Start();
        }
    }

    void SceneEditor::ClearSelection(EditorTransaction* transaction)
    {
        transaction->CreateAction<ObjectSelectionAction>(*this, ObjectSelectionAction::Type::ClearSelection, UUID{})->Commit();
    }

    void SceneEditor::SelectObject(GameObject& object, EditorTransaction* transaction)
    {
        transaction->CreateAction<ObjectSelectionAction>(*this, ObjectSelectionAction::Type::Select, object.GetUUID())->Commit();
    }

    void SceneEditor::DeselectObject(GameObject& object, EditorTransaction* transaction)
    {
        transaction->CreateAction<ObjectSelectionAction>(*this, ObjectSelectionAction::Type::Deselect, object.GetUUID())->Commit();
    }

    void SceneEditor::ClearSelectionNoHistory()
    {
        for (const auto& it : selectedObjects)
        {
            onGameObjectDeselectionHandler.Invoke(it.first);
        }
        selectedObjects.Clear();
    }

    void SceneEditor::SelectObjectNoHistory(GameObject& object)
    {
        selectedObjects.Emplace(object.GetUUID());
        onGameObjectSelectionHandler.Invoke(object.GetUUID());
    }

    void SceneEditor::SelectObjectsNoHistory(Span<UUID> ids)
    {
        for (UUID& id : ids)
        {
            if (GameObject* gameObject = GetActiveScene()->FindObjectByUUID(id))
            {
                SelectObjectNoHistory(*gameObject);
            }
        }
    }

    void SceneEditor::DeselectObjectNoHistory(GameObject& object)
    {
        onGameObjectDeselectionHandler.Invoke(object.GetUUID());
        selectedObjects.Erase(object.GetUUID());
    }

    bool SceneEditor::IsSelected(GameObject& object) const
    {
        return selectedObjects.Has(object.GetUUID());
    }

    bool SceneEditor::IsParentOfSelected(GameObject& object) const
    {
        for (const auto& it : selectedObjects)
        {
            if (GameObject* object = GetActiveScene()->FindObjectByUUID(it.first))
            {
                if (GameObject* parent = object->GetParent())
                {
                    if (parent == object)
                    {
                        return true;
                    }
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
        Editor::CreateTransaction()->CreateAction<CreateGameObjectAction>(*this, prefab, checkSelected)->Commit();
    }

    void SceneEditor::DuplicateSelected()
    {
        if (!selectedObjects.Empty())
        {
            Editor::CreateTransaction()->CreateAction<DuplicateObjectAction>(*this)->Commit();
        }
    }

    bool SceneEditor::IsValidSelection()
    {
        return GetActiveScene() != nullptr;
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

    void SceneEditor::MoveEntities(GameObject* parent, usize index, Span<GameObject*> objects)
    {
        MoveObjectsAction* action = Editor::CreateTransaction()->CreateAction<MoveObjectsAction>(*this, parent, index, objects);
        Editor::ExecuteOnMainThread([action]
        {
            action->Commit();
        });
    }

    void SceneEditor::ChangeParent(GameObject* parent, Span<GameObject*> objects)
    {
        ChangeParentAction* action = Editor::CreateTransaction()->CreateAction<ChangeParentAction>(*this, parent, objects);
        Editor::ExecuteOnMainThread([action]
        {
            action->Commit();
        });
    }

    Array<UUID> SceneEditor::GetSelectObjectUUIDS() const
    {
        Array<UUID> ret;
        ret.Reserve(selectedObjects.Size());
        for (auto& it : selectedObjects)
        {
            ret.EmplaceBack(it.first);
        }
        return ret;
    }

    bool SceneEditor::IsSimulating() const
    {
        return simulationScene != nullptr;
    }

    void SceneEditor::StartSimulation()
    {
        if (!editorScene) return;
        shouldStartSimulation = true;
    }

    void SceneEditor::StopSimulation()
    {
        if (!editorScene) return;
        shouldStopSimulation = true;
    }

    void SceneEditor::DoUpdate()
    {
        if (editorScene)
        {
            if (shouldStartSimulation)
            {
                simulationScene = Alloc<Scene>();
                simulationScene->SetUUID(editorScene->GetUUID());

                JsonArchiveWriter writer;
                JsonArchiveReader reader(JsonArchiveWriter::Stringify(editorScene->Serialize(writer), false));
                simulationScene->Deserialize(reader, reader.GetRoot());

                shouldStartSimulation = false;
            }
            if (simulationScene)
            {
                if (shouldStopSimulation)
                {
                    DestroyAndFree(simulationScene);
                    shouldStopSimulation = false;
                    simulationScene = nullptr;
                }
                else
                {
                    simulationScene->Update();
                }
            }
            else
            {
                editorScene->FlushQueues();
            }
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
        Registry::Type<ObjectSelectionAction>();
        Registry::Type<ChangeParentAction>();
        Registry::Type<MoveObjectsAction>();
    }
}
