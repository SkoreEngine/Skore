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

#include "SceneEditor.hpp"

#include "Skore/App.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Events.hpp"
#include "Skore/Asset/AssetEditor.hpp"
#include "Skore/Asset/AssetFileOld.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Commands/SceneCommands.hpp"
#include "Skore/Commands/UndoRedoSystem.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::SceneEditor");
		EventHandler<OnEntitySelection>   onEntitySelectionHandler{};
		EventHandler<OnEntityDeselection> onEntityDeselectionHandler{};
	}

	SceneEditor::SceneEditor(EditorWorkspace& workspace) : m_workspace(workspace)
	{
		//TODO : need to open the default scene.
		// AssetFile* assetFile = AssetEditor::CreateAsset(AssetEditor::GetProject(), TypeInfo<Scene>::ID(), "Default Scene");
		// OpenScene(assetFile);
		Event::Bind<OnUpdate, &SceneEditor::DoUpdate>(this);
	}

	SceneEditor::~SceneEditor()
	{
		if (m_simulationScene)
		{
			DestroyAndFree(m_simulationScene);
			m_simulationScene = nullptr;
		}

		Event::Unbind<OnUpdate, &SceneEditor::DoUpdate>(this);
	}

	EditorWorkspace& SceneEditor::GetWorkspace() const
	{
		return m_workspace;
	}

	void SceneEditor::DoUpdate() const
	{
		if (m_simulationScene)
		{
			m_simulationScene->Update(App::DeltaTime());
		}
		else if (m_scene)
		{
			m_scene->FlushQueues();
		}
	}


	void SceneEditor::OpenScene(AssetFileOld* assetFile)
	{
		//TODO:Reset undo redo
		StopSimulation();
		ClearSelection();
		m_lockedEntities.Clear();

		m_assetFile = assetFile;
		m_scene = assetFile->GetInstance()->Cast<Scene>();

		logger.Debug("Scene {} opened", assetFile->GetFileName());
	}

	bool SceneEditor::IsLoaded() const
	{
		return GetCurrentScene() != nullptr;
	}

	bool SceneEditor::IsReadOnly() const
	{
		return m_assetFile->GetAssetTypeFile() != AssetFileType::Asset;
	}

	Scene* SceneEditor::GetCurrentScene() const
	{
		return m_simulationScene ? m_simulationScene : m_scene;
	}

	Entity* SceneEditor::GetRoot() const
	{
		if (Scene* scene = GetCurrentScene())
		{
			return scene->GetRootEntity();
		}
		return nullptr;
	}

	bool SceneEditor::IsSelected(UUID entity) const
	{
		if (!entity)
		{
			return false;
		}

		return m_selectedEntities.Has(entity);
	}

	const HashSet<UUID>& SceneEditor::GetSelectedEntities() const
	{
		return m_selectedEntities;
	}

	bool SceneEditor::HasSelectedEntities() const
	{
		return !m_selectedEntities.Empty();
	}

	void SceneEditor::ClearSelection()
	{
		if (m_selectedEntities.Empty())
		{
			return;
		}

		Ref<Transaction> transaction = BeginSceneTransaction("Clear Selection");
		transaction->AddCommand(Alloc<ClearSelectionCommand>(this));
		UndoRedoSystem::EndTransaction(transaction);
	}

	bool SceneEditor::IsParentOfSelected(Entity* entity) const
	{
		if (entity == nullptr) return false;

		for (const UUID& selectedUUID : m_selectedEntities)
		{
			Entity* selected = GetCurrentScene()->FindEntityByUUID(selectedUUID);
			if (selected && selected->IsChildOf(entity))
			{
				return true;
			}
		}
		return false;
	}

	void SceneEditor::Create()
	{
		Scene* scene = GetCurrentScene();

		if (!scene || !m_assetFile)
		{
			return;
		}

		Ref<Transaction> transaction = BeginSceneTransaction("Create Entity");
		transaction->AddCommand(Alloc<ClearSelectionCommand>(this));

		if (m_selectedEntities.Empty())
		{
			transaction->AddCommand(Alloc<CreateEntityCommand>(this, GetRoot(), "Entity"));
		}
		else
		{
			for (const auto& uuid : m_selectedEntities)
			{
				if (Entity* parentEntity = scene->FindEntityByUUID(uuid))
				{
					transaction->AddCommand(Alloc<CreateEntityCommand>(this, parentEntity, "Entity"));
				}
			}
		}

		UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::AddComponent(Entity* entity, TypeID componentTypeID)
	{
		if (!entity || !m_assetFile || IsLocked(entity) || componentTypeID == 0)
		{
			return;
		}

		Ref<Transaction> transaction = BeginSceneTransaction("Add Component");
		transaction->AddCommand(Alloc<AddComponentCommand>(this, entity, componentTypeID));
		UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::RemoveComponent(Entity* entity, Component* component)
	{
		if (!entity || !m_assetFile || IsLocked(entity) || component == nullptr)
		{
			return;
		}

		Ref<Transaction> transaction = BeginSceneTransaction("Remove Component");
		transaction->AddCommand(Alloc<RemoveComponentCommand>(this, entity, component));
		UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::ResetComponent(Entity* entity, Component* component)
	{

	}

	void SceneEditor::DeleteSelected()
	{
		Scene* scene = GetCurrentScene();

		if (m_selectedEntities.Empty() || !m_assetFile)
		{
			return;
		}

		Ref<Transaction> transaction = BeginSceneTransaction("Delete Entities");
		for (const auto& uuid : m_selectedEntities)
		{
			Entity* entity = scene->FindEntityByUUID(uuid);
			if (entity && !m_lockedEntities.Has(uuid))
			{
				transaction->AddCommand(Alloc<DestroyEntityCommand>(this, entity));
			}
		}
		UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::DuplicateSelected()
	{
		// if (m_selectedEntities.Empty() || !m_assetFile)
		// {
		// 	return;
		// }
		//
		// Ref<Transaction> transaction = UndoRedoSystem::BeginTransaction(TransactionCategory::Entity, "Duplicate Entities");
		//
		// Array<Entity*> originalEntities;
		// Array<Entity*> duplicatedEntities;
		//
		// originalEntities.Reserve(m_selectedEntities.Size());
		//
		// for (const auto& uuid : m_selectedEntities)
		// {
		// 	Entity* entity = m_scene->FindEntityByUUID(uuid);
		// 	if (entity && !m_lockedEntities.Has(uuid))
		// 	{
		// 		originalEntities.EmplaceBack(entity);
		// 	}
		// }
		//
		// for (const auto& originalObj : originalEntities)
		// {
		// 	Entity* duplicate = Duplicate(originalObj);
		// 	if (duplicate)
		// 	{
		// 		duplicatedEntities.EmplaceBack(duplicate);
		// 	}
		// }
		//
		// SetSelectedEntities(duplicatedEntities, false);
		// UndoRedoSystem::EndTransaction(transaction);
	}

	Entity* SceneEditor::Duplicate(Entity* entity)
	{
		if (!entity || !m_assetFile || IsLocked(entity))
		{
			return nullptr;
		}

		// Ref<Transaction>        transaction = UndoRedoSystem::BeginTransaction("Duplicate Entity");
		// DuplicateEntityCommand* command = Alloc<DuplicateEntityCommand>(this, entity);
		// transaction->AddCommand(command);
		// UndoRedoSystem::EndTransaction(transaction);
		//
		// return command->GetDuplicatedEntity();

		return nullptr;
	}

	void SceneEditor::ChangeParent(Entity* newParent, const Array<Entity*>& objects)
	{
		if (!m_assetFile || !newParent)
		{
			return;
		}

		// Ref<Transaction> transaction = UndoRedoSystem::BeginTransaction("Change Parent");
		//
		// for (const auto& obj : objects)
		// {
		// 	if (!obj || IsLocked(obj))
		// 	{
		// 		continue;
		// 	}
		//
		// 	if (obj == newParent || newParent->IsChildOf(obj))
		// 	{
		// 		continue;
		// 	}
		//
		// 	ChangeParentCommand* command = Alloc<ChangeParentCommand>(this, obj, newParent);
		// 	transaction->AddCommand(command);
		// }
		//
		// UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::SelectEntity(UUID entity, bool clearSelection)
	{
		if (!entity && !clearSelection)
		{
			return;
		}

		if (m_selectedEntities.Has(entity))
		{
			return;
		}

		HashSet<UUID> oldSelection = m_selectedEntities;
		HashSet<UUID> newSelection = clearSelection ? HashSet<UUID>{} : m_selectedEntities;

		if (entity)
		{
			if (newSelection.Has(entity))
			{
				newSelection.Erase(entity);
			}
			else
			{
				newSelection.Insert(entity);
			}
		}

		UpdateSelection(oldSelection, newSelection);
	}

	void SceneEditor::DeselectEntity(UUID entity)
	{
		if (!entity || !IsSelected(entity))
		{
			return;
		}

		HashSet<UUID> oldSelection = m_selectedEntities;
		HashSet<UUID> newSelection = m_selectedEntities;
		newSelection.Erase(entity);

		UpdateSelection(oldSelection, newSelection);
	}

	void SceneEditor::SetSelectedEntities(Span<UUID> entities)
	{
		HashSet<UUID> oldSelection = m_selectedEntities;
		HashSet<UUID> newSelection;

		for (const auto& obj : entities)
		{
			newSelection.Insert(obj);
		}

		UpdateSelection(oldSelection, newSelection);
	}

	void SceneEditor::UpdateSelection(const HashSet<UUID>& oldSelection, const HashSet<UUID>& newSelection)
	{
		bool selectionChanged = (oldSelection != newSelection);
		if (!selectionChanged)
		{
			return;
		}

		Ref<Transaction>  transaction = BeginSceneTransaction("Selection Change");
		SelectionCommand* command = Alloc<SelectionCommand>(this, oldSelection, newSelection);
		transaction->AddCommand(command);
		UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::InternalClearSelection()
	{
		for (UUID selected : m_selectedEntities)
		{
			onEntityDeselectionHandler.Invoke(m_workspace.GetId(), selected);
		}
		m_selectedEntities.Clear();
	}

	void SceneEditor::InternalDeselectEntity(UUID entityUUID)
	{
		if (const auto& it = m_selectedEntities.Find(entityUUID))
		{
			onEntityDeselectionHandler.Invoke(m_workspace.GetId(), *it);
			m_selectedEntities.Erase(it);
		}
	}

	void SceneEditor::InternalSelectEntity(UUID entityUUID)
	{
		if (!m_selectedEntities.Has(entityUUID))
		{
			m_selectedEntities.Insert(entityUUID);
			onEntitySelectionHandler.Invoke(m_workspace.GetId(), entityUUID);
		}
	}

	Ref<Transaction> SceneEditor::BeginSceneTransaction(StringView name) const
	{
		return UndoRedoSystem::BeginTransaction(m_simulationScene ? TransactionCategory::Simulation : TransactionCategory::Entity, name);
	}

	void SceneEditor::Rename(Entity* entity, StringView newName)
	{
		if (!entity || !m_assetFile || IsLocked(entity))
		{
			return;
		}

		Ref<Transaction> transaction = BeginSceneTransaction("Rename Entity");
		RenameEntityCommand* command = Alloc<RenameEntityCommand>(this, entity, newName);
		transaction->AddCommand(command);
		UndoRedoSystem::EndTransaction(transaction);
	}

	void SceneEditor::UpdateTransform(Entity* entity, const Transform& oldTransform, const Transform& newTransform)
	{
		Ref<Transaction> transaction = BeginSceneTransaction("Move Entity");
		transaction->AddCommand(Alloc<EntityMoveCommand>(this, entity, oldTransform, newTransform));
		UndoRedoSystem::EndTransaction(transaction);
	}

	bool SceneEditor::IsLocked(Entity* entity)
	{
		if (!entity)
		{
			return false;
		}
		return m_lockedEntities.Has(entity->GetUUID());
	}

	void SceneEditor::SetLocked(Entity* entity, bool locked)
	{
		if (!entity)
		{
			return;
		}

		if (locked)
		{
			m_lockedEntities.Insert(entity->GetUUID());
		}
		else
		{
			m_lockedEntities.Erase(entity->GetUUID());
		}
	}

	void SceneEditor::SetActive(Entity* entity, bool active)
	{
		if (!entity || !m_assetFile || IsLocked(entity))
		{
			return;
		}

		// Ref<Transaction> transaction = UndoRedoSystem::BeginTransaction(active ? "Activate Entity" : "Deactivate Entity");
		// SetActiveCommand* command = Alloc<SetActiveCommand>(this, entity, active);
		// transaction->AddCommand(command);
		// UndoRedoSystem::EndTransaction(transaction);
	}

	bool SceneEditor::IsSimulating() const
	{
		return m_isSimulating;
	}

	void SceneEditor::StartSimulation()
	{
		Scene* scene = GetCurrentScene();

		if (m_isSimulating || !scene)
		{
			return;
		}

		m_simulationScene = Alloc<Scene>();

		{
			BinaryArchiveWriter writer;
			m_scene->Serialize(writer);
			BinaryArchiveReader reader(writer.GetData());
			m_simulationScene->Deserialize(reader);
		}


		m_isSimulating = true;
	}

	void SceneEditor::StopSimulation()
	{
		if (!m_isSimulating)
		{
			return;
		}

		if (m_simulationScene)
		{
			DestroyAndFree(m_simulationScene);
			m_simulationScene = nullptr;
		}

		m_isSimulating = false;
	}

	void SceneEditor::MarkDirty() const
	{
		if (m_assetFile)
		{
			m_assetFile->MarkDirty();
		}
	}
}
