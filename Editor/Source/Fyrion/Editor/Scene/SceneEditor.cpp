#include "SceneEditor.hpp"

#include "Fyrion/Core/Logger.hpp"
#include "Fyrion/Editor/Asset/AssetEditor.hpp"
#include "Fyrion/Editor/ImGui/ImGuiEditor.hpp"
#include "Fyrion/Scene/Component/Component.hpp"
#include "Fyrion/Scene/Component/TransformComponent.hpp"

namespace Fyrion
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Fyrion::Scene");
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
        selectedObjects.Clear();
        onGameObjectSelectionHandler.Invoke(nullptr);
    }

    void SceneEditor::SelectObject(GameObject& object)
    {
        selectedObjects.Emplace(&object);
        onGameObjectSelectionHandler.Invoke(&object);
    }

    void SceneEditor::DeselectObject(GameObject& object)
    {
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
        object.SetName(newName);
        assetFile->currentVersion++;
    }

    void SceneEditor::DestroySelectedObjects()
    {
        for (auto it : selectedObjects)
        {
            it.first->Destroy();
        }
        ClearSelection();
        assetFile->currentVersion++;
    }

    void SceneEditor::CreateGameObject(Scene* prefab, bool checkSelected)
    {
        if (scene == nullptr) return;

        String prefabName = "";
        if (prefab != nullptr)
        {
            AssetFile* assetFile = AssetEditor::FindAssetFileByUUID(prefab->GetUUID());
            prefabName = assetFile->fileName;
        }

        if (!checkSelected || selectedObjects.Empty())
        {
            GameObject* gameObject = scene->GetRootObject().Create(prefab != nullptr ? prefab: nullptr);
            gameObject->SetName(!prefabName.Empty() ? prefabName : "Object");

            ClearSelection();
            SelectObject(*gameObject);
        }
        else
        {
            Array<GameObject*> newObjects;
            newObjects.Reserve(selectedObjects.Size());

            for(auto it: selectedObjects)
            {
                GameObject* gameObject = it.first->Create(prefab != nullptr ? prefab: nullptr);
                gameObject->SetName(!prefabName.Empty() ? prefabName : "Object");
                newObjects.EmplaceBack(gameObject);
            }

            ClearSelection();

            for(GameObject* object : newObjects)
            {
                SelectObject(*object);
            }
        }
        assetFile->currentVersion++;
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

            for(TypeID dependency : componentDesc->dependencies)
            {
                gameObject->GetOrAddComponent(dependency);
            }
        }

        gameObject->AddComponent(typeHandler, UUID::RandomUUID());
        assetFile->currentVersion++;
    }

    void SceneEditor::ResetComponent(GameObject* gameObject, Component* component)
    {

    }

    void SceneEditor::RemoveComponent(GameObject* gameObject, Component* component)
    {
        for(Component* otherComps: gameObject->GetComponents())
        {
            if (otherComps == component) continue;

            if (const ComponentDesc* componentDesc = otherComps->typeHandler->GetAttribute<ComponentDesc>())
            {
                for(TypeID dependency : componentDesc->dependencies)
                {
                    if (dependency == component->typeHandler->GetTypeInfo().typeId)
                    {
                        gameObject->RemoveComponent(otherComps);
                    }
                }
            }
        }

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
        if (gameObject->GetPrefab() != nullptr)
        {
            gameObject->AddComponentOverride(transformComponent);
        }
        assetFile->currentVersion++;
    }

    void SceneEditor::RemoveComponentOverride(GameObject* gameObject, Component* component)
    {
        gameObject->RemoveComponentOverride(component);
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
}
