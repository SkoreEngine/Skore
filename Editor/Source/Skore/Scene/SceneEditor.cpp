#include "Skore/Scene/SceneEditor.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Events.hpp"
#include "Skore/IO/Input.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/SceneManager.hpp"

//TODO: selection between RID and Entity* need to be merged, maybe with UUID?

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::SceneEditor");

	static EventHandler<OnEntitySelection>   onEntitySelectionHandler{};
	static EventHandler<OnEntityDeselection> onEntityDeselectionHandler{};

	static EventHandler<OnEntityDebugSelection>   onEntityDebugSelectionHandler{};
	static EventHandler<OnEntityDebugDeselection> onEntityDebugDeselectionHandler{};


	struct SceneEditorSelection
	{
		enum
		{
			Workspace,
			SelectedEntities
		};
	};

	struct SceneEditorState
	{
		enum
		{
			Workspace,
			OpenEntity,
			OpenScene
		};
	};

	SceneEditor::SceneEditor(EditorWorkspace& workspace) : m_workspace(workspace)
	{
		{
			m_state = Resources::Create<SceneEditorState>();
			ResourceObject stateObject = Resources::Write(m_state);
			stateObject.SetUInt(SceneEditorState::Workspace, workspace.GetId());
			stateObject.Commit();
		}

		{
			m_selection = Resources::Create<SceneEditorSelection>();
			ResourceObject selectionObject = Resources::Write(m_selection);
			selectionObject.SetUInt(SceneEditorState::Workspace, workspace.GetId());
			selectionObject.Commit();
		}

		Resources::FindType<SceneEditorSelection>()->RegisterEvent(ResourceEventType::Changed, OnSelectionChange, this);
		Resources::FindType<SceneEditorState>()->RegisterEvent(ResourceEventType::Changed, OnStateChange, this);
		Resources::FindType<EntityResource>()->RegisterEvent(ResourceEventType::Changed, OnEntityChange, this);
	}

	SceneEditor::~SceneEditor()
	{
		Resources::FindType<SceneEditorSelection>()->UnregisterEvent(ResourceEventType::Changed, OnSelectionChange, this);
		Resources::FindType<SceneEditorState>()->UnregisterEvent(ResourceEventType::Changed, OnStateChange, this);
		Resources::FindType<EntityResource>()->UnregisterEvent(ResourceEventType::Changed, OnEntityChange, this);

		Resources::Destroy(m_selection);
		Resources::Destroy(m_state);
	}

	void SceneEditor::OpenEntity(RID entity)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Entity On Editor");
		ResourceObject stateObject = Resources::Write(m_state);
		stateObject.SetReference(SceneEditorState::OpenEntity, entity);
		stateObject.SetReference(SceneEditorState::OpenScene, {});
		stateObject.Commit(scope);
	}

	void SceneEditor::OpenScene(RID scene)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Scene On Editor");
		ResourceObject stateObject = Resources::Write(m_state);
		stateObject.SetReference(SceneEditorState::OpenEntity, {});
		stateObject.SetReference(SceneEditorState::OpenScene, scene);
		stateObject.Commit(scope);
	}

	RID SceneEditor::GetOpenedResource() const
	{
		ResourceObject stateObject = Resources::Read(m_state);
		if (RID entity = stateObject.GetReference(SceneEditorState::OpenEntity))
		{
			return entity;
		}
		return stateObject.GetReference(SceneEditorState::OpenScene);
	}

	bool SceneEditor::IsOpenedScene() const
	{
		ResourceObject stateObject = Resources::Read(m_state);
		return stateObject.GetReference(SceneEditorState::OpenScene) != RID{};
	}

	bool SceneEditor::IsReadOnly() const
	{
		//TODO
		return false;
	}

	u32 SceneEditor::GetChildrenField(RID parent)
	{
		if (Resources::GetType(parent)->GetID() == TypeInfo<SceneResource>::ID())
		{
			return SceneResource::Entities;
		}
		return EntityResource::Children;
	}

	void SceneEditor::Create(EntityCreationType type, RID entityAsset)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Create Entity");
		Span<RID>      selectedEntities = GetSelectedEntities();

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);

		auto createEntity = [&](RID parent)
		{
			RID newEntity;

			if (!entityAsset)
			{
				newEntity = Resources::Create<EntityResource>(UUID::RandomUUID(), scope);
				ResourceObject newEntityObject = Resources::Write(newEntity);
				switch (type)
				{
					case EntityCreationType::Empty:
						newEntityObject.SetString(EntityResource::Name, "New Entity");
						break;
					case EntityCreationType::Cube:
						newEntityObject.SetString(EntityResource::Name, "Cube");
						break;
					case EntityCreationType::Sphere:
						newEntityObject.SetString(EntityResource::Name, "Sphere");
						break;
					case EntityCreationType::Cylinder:
						newEntityObject.SetString(EntityResource::Name, "Cylinder");
						break;
					case EntityCreationType::Capsule:
						newEntityObject.SetString(EntityResource::Name, "Capsule");
						break;
					case EntityCreationType::Plane:
						newEntityObject.SetString(EntityResource::Name, "Plane");
						break;
					case EntityCreationType::Sprite2D:
						break;
				}
				newEntityObject.Commit(scope);
			}
			else
			{
				newEntity = Resources::CreateFromPrototype(entityAsset, UUID::RandomUUID(), scope);
			}

			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectList(GetChildrenField(parent), newEntity);
			parentObject.Commit(scope);

			selectionObject.AddToReferenceArray(SceneEditorSelection::SelectedEntities, newEntity);
		};

		if (selectedEntities.Empty())
		{
			createEntity(GetOpenedResource());
		}
		else
		{
			for (RID parent : selectedEntities)
			{
				createEntity(parent);
			}
		}

		selectionObject.Commit(scope);
	}

	RID SceneEditor::CreateFromAsset(RID parent, RID entityAsset, UndoRedoScope* scope)
	{
		RID newEntity = Resources::CreateFromPrototype(entityAsset, UUID::RandomUUID(), scope);

		ResourceObject parentObject = Resources::Write(parent);
		parentObject.AddToSubObjectList(GetChildrenField(parent), newEntity);
		parentObject.Commit(scope);

		return newEntity;
	}

	RID SceneEditor::Clone(RID parent, RID entityAsset, UndoRedoScope* scope)
	{
		RID            newEntity = Resources::Clone(entityAsset, UUID::RandomUUID(), scope);
		ResourceObject parentObject = Resources::Write(parent);
		parentObject.AddToSubObjectList(GetChildrenField(parent), newEntity);
		parentObject.Commit(scope);
		return newEntity;
	}

	void SceneEditor::DestroySelected()
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Destroy Entity");
		for (RID selected : GetSelectedEntities())
		{
			Resources::Destroy(selected, scope);
		}
	}

	void SceneEditor::DuplicateSelected()
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Destroy Entity");

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);

		for (RID selected : GetSelectedEntities())
		{
			RID newEntity = Resources::Clone(selected, UUID::RandomUUID(), scope);

			RID            parentRID = Resources::GetParent(selected);
			ResourceObject parentObject = Resources::Write(parentRID);
			parentObject.AddToSubObjectList(GetChildrenField(parentRID), newEntity);
			parentObject.Commit(scope);

			selectionObject.AddToReferenceArray(SceneEditorSelection::SelectedEntities, newEntity);
		}
		selectionObject.Commit(scope);
	}

	void SceneEditor::ChangeParentOfSelected(RID newParent)
	{
		ChangeParentOfSelected(newParent, Editor::CreateUndoRedoScope("Change Parent Entity"));
	}

	void SceneEditor::ChangeParentOfSelected(RID newParent, UndoRedoScope* scope)
	{
		for (RID selected : GetSelectedEntities())
		{
			RID            oldParentRID = Resources::GetParent(selected);
			ResourceObject oldParent = Resources::Write(oldParentRID);
			oldParent.RemoveFromSubObjectList(GetChildrenField(oldParentRID), selected);
			oldParent.Commit(scope);

			ResourceObject newParentObject = Resources::Write(newParent);
			newParentObject.AddToSubObjectList(GetChildrenField(newParent), selected);
			newParentObject.Commit(scope);
		}
	}

	void SceneEditor::MoveSelectedBefore(RID moveTo)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move Entity");

		if (!moveTo)
		{
			ChangeParentOfSelected(GetOpenedResource(), scope);
			return;
		}

		RID parent = Resources::GetParent(moveTo);

		Array<RID> entitiesToMove;
		bool       moveToIsSelected = false;

		//1 - sort by order in hierarchy, not by order of selection
		{
			HashSet<RID> parents;
			HashSet<RID> selectedEntities = ToHashSet(GetSelectedEntities());
			moveToIsSelected = selectedEntities.Has(moveTo);

			entitiesToMove.Reserve(selectedEntities.Size());

			for (RID selected : GetSelectedEntities())
			{
				parents.Insert(Resources::GetParent(selected));
			}

			for (RID parentOfSelected : parents)
			{
				ResourceObject parentObject = Resources::Write(parentOfSelected);
				parentObject.IterateSubObjectList(GetChildrenField(parentOfSelected), [&](RID child)
				{
					if (selectedEntities.Has(child))
					{
						entitiesToMove.EmplaceBack(child);
					}
				});
			}
		}
		usize index = SIZE_MAX;

		u32 parentChildField = GetChildrenField(parent);

		if (moveToIsSelected)
		{
			ResourceObject parentObject = Resources::Read(parent);
			Span<RID>      children = parentObject.GetSubObjectList(parentChildField);
			index = FindFirstIndex(children.begin(), children.end(), moveTo);
		}

		for (RID selected : entitiesToMove)
		{
			RID            oldParentRID = Resources::GetParent(selected);
			ResourceObject oldParent = Resources::Write(oldParentRID);
			oldParent.RemoveFromSubObjectList(GetChildrenField(oldParentRID), selected);
			oldParent.Commit(scope);
		}

		//get index
		if (!moveToIsSelected)
		{
			ResourceObject parentObject = Resources::Read(parent);
			Span<RID>      children = parentObject.GetSubObjectList(parentChildField);
			index = FindFirstIndex(children.begin(), children.end(), moveTo);
		}

		if (index == SIZE_MAX)
		{
			logger.Error("something went wrong, index not found");
		}

		ResourceObject newParentObject = Resources::Write(parent);
		newParentObject.AddToSubObjectListAt(parentChildField, entitiesToMove, index);
		newParentObject.Commit(scope);
	}

	void SceneEditor::AddBackToThisInstance(RID entity, RID prototype)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Back To This Instance");

		RID newInstance = Resources::CreateFromPrototype(prototype, UUID::RandomUUID(), scope);

		ResourceObject entityObject = Resources::Write(entity);
		entityObject.AddToSubObjectList(GetChildrenField(entity), newInstance);
		entityObject.Commit(scope);

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);
		selectionObject.AddToReferenceArray(SceneEditorSelection::SelectedEntities, newInstance);
		selectionObject.Commit(scope);
	}

	void SceneEditor::ClearSelection()
	{
		ClearDebugEntitySelection();
		if (HasSelectedEntities())
		{
			ClearSelection(Editor::CreateUndoRedoScope("Clear selection"));
		}
	}

	void SceneEditor::SelectEntity(RID entity, bool clearSelection)
	{
		if (IsSelected(entity)) return;

		SelectEntity(entity, clearSelection, Editor::CreateUndoRedoScope("Select Entity"));
	}

	void SceneEditor::SelectEntity(RID entity, bool clearSelection, UndoRedoScope* scope)
	{
		if (IsSelected(entity)) return;

		ResourceObject selectionObject = Resources::Write(m_selection);
		if (clearSelection)
		{
			selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);
		}
		selectionObject.AddToReferenceArray(SceneEditorSelection::SelectedEntities, entity);
		selectionObject.Commit(scope);
	}

	void SceneEditor::SelectEntities(Span<RID> entities, bool clearSelection)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Select Entities");
		ResourceObject selectionObject = Resources::Write(m_selection);
		if (clearSelection)
		{
			selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);
		}
		//performnace: add in one command.
		for (RID entity : entities)
		{
			selectionObject.AddToReferenceArray(SceneEditorSelection::SelectedEntities, entity);
		}
		selectionObject.Commit(scope);
	}

	void SceneEditor::DeselectEntity(RID entity)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Deselect Entity");
		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.RemoveFromReferenceArray(SceneEditorSelection::SelectedEntities, entity);
		selectionObject.Commit(scope);
	}

	bool SceneEditor::IsSelected(RID entity)
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return selectionObject.HasOnReferenceArray(SceneEditorSelection::SelectedEntities, entity);
	}

	bool SceneEditor::IsParentOfSelected(RID entity)
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		for (RID selected : selectionObject.GetReferenceArray(SceneEditorSelection::SelectedEntities))
		{
			RID parent = Resources::GetParent(selected);
			while (parent)
			{
				if (parent == entity)
				{
					return true;
				}
				parent = Resources::GetParent(parent);
			}
		}
		return false;
	}

	bool SceneEditor::HasSelectedEntities() const
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return !selectionObject.GetReferenceArray(SceneEditorSelection::SelectedEntities).Empty();
	}

	Span<RID> SceneEditor::GetSelectedEntities() const
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return selectionObject.GetReferenceArray(SceneEditorSelection::SelectedEntities);
	}

	void SceneEditor::SelectEntity(Entity* entity, bool clearSelection)
	{
		if (clearSelection)
		{
			ClearDebugEntitySelection();
		}
		m_selectedEntities.Emplace(entity);

		onEntityDebugSelectionHandler.Invoke(m_workspace.GetId(), entity);
	}

	bool SceneEditor::IsSelected(Entity* entity) const
	{
		return m_selectedEntities.Has(entity);
	}

	void SceneEditor::SetActivated(RID entity, bool activated)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Activate Entity");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.SetBool(EntityResource::Deactivated, !activated);
		entityObject.Commit(scope);
	}

	void SceneEditor::SetLocked(RID entity, bool locked)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Lock Entity");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.SetBool(EntityResource::Locked, locked);
		entityObject.Commit(scope);
	}

	void SceneEditor::Rename(RID entity, StringView newName)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Rename Entity");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.SetString(EntityResource::Name, newName);
		entityObject.Commit(scope);
	}

	void SceneEditor::AddComponent(RID entity, TypeID componentId)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Component");
		RID            component = Resources::Create(componentId, UUID::RandomUUID());
		Resources::Write(component).Commit(scope);

		ResourceObject entityObject = Resources::Write(entity);
		entityObject.AddToSubObjectList(EntityResource::Components, component);
		entityObject.Commit(scope);
	}

	void SceneEditor::ResetComponent(RID entity, RID component)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Reset Component");
		Resources::Reset(component, scope);
	}

	void SceneEditor::RemoveComponent(RID entity, RID component)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Remove Component");
		Resources::Destroy(component, scope);

		// ResourceObject entityObject = Resources::Write(entity);
		// entityObject.RemoveFromSubObjectList(EntityResource::Components, component);
		// entityObject.Commit(scope);
	}

	void SceneEditor::MoveComponentTo(RID component, u32 newIndex)
	{
		RID entity = Resources::GetParent(component);
		{
			ResourceObject entityObject = Resources::Read(entity);
			Span<RID>      components = entityObject.GetSubObjectList(EntityResource::Components);
			u32            currentIndex = FindFirstIndex(components.begin(), components.end(), component);
			if (currentIndex == newIndex) return;
		}

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move Component");

		ResourceObject entityObject = Resources::Write(entity);
		entityObject.RemoveFromSubObjectList(EntityResource::Components, component);
		entityObject.AddToSubObjectListAt(EntityResource::Components, {&component, 1}, newIndex);
		entityObject.Commit(scope);
	}

	bool SceneEditor::IsSimulationRunning() const
	{
		return SceneManager::GetActiveScene() != nullptr;
	}

	void SceneEditor::StartSimulation()
	{
		m_shouldStartSimulation = true;
	}

	void SceneEditor::StopSimulation()
	{
		m_shouldStopSimulation = true;
	}

	void SceneEditor::PauseSimulation()
	{
		//TODO
	}

	Scene* SceneEditor::GetCurrentScene() const
	{
		if (Scene* activeScene = SceneManager::GetActiveScene())
		{
			return activeScene;
		}

		if (m_editorScene)
		{
			return m_editorScene.get();
		}

		return nullptr;
	}

	void SceneEditor::DoUpdate()
	{
		if (!m_simulationScene && m_shouldStartSimulation)
		{
			RID openedResource = GetOpenedResource();
			if (IsOpenedScene())
			{
				m_simulationScene = std::make_shared<Scene>(openedResource, true);
			}
			else
			{
				m_simulationScene = std::make_shared<Scene>(TypedRID<EntityResource>(openedResource), true);
			}
			SceneManager::SetActiveScene(m_simulationScene.get());
		}

		if (m_simulationScene && m_shouldStopSimulation)
		{
			m_simulationScene = {};
			SceneManager::SetActiveScene(nullptr);
			Input::DisableInputs(false);
		}

		if (SceneManager::GetActiveScene() == nullptr && m_editorScene)
		{
			m_editorScene->ExecuteEvents(false);

			for (RID rid : GetSelectedEntities())
			{
				if (Entity* entity = m_editorScene->FindEntityByRID(rid))
				{
					EntityEventDesc event;
					event.type = EntityEventType::EntityIsSelectedOnEditor;
					entity->NotifyEvent(event, false);
				}
			}
		}

		m_shouldStartSimulation = false;
		m_shouldStopSimulation = false;
	}

	const HashSet<Entity*>& SceneEditor::GetSelectionCache() const
	{
		return m_selectionCache;
	}


	void SceneEditor::OnStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		SceneEditor* sceneEditor = static_cast<SceneEditor*>(userData);

		if (newValue.GetUInt(SceneEditorSelection::Workspace) != sceneEditor->m_workspace.GetId())
		{
			return;
		}

		RID oldOpened{};
		RID newOpened{};

		if (oldValue)
		{
			if (RID rid = oldValue.GetReference(SceneEditorState::OpenEntity))
			{
				oldOpened = rid;
			}
			else if (RID rid = oldValue.GetReference(SceneEditorState::OpenScene))
			{
				oldOpened = rid;
			}
		}

		if (newValue)
		{
			if (RID rid = newValue.GetReference(SceneEditorState::OpenEntity))
			{
				newOpened = rid;
			}
			else if (RID rid = newValue.GetReference(SceneEditorState::OpenScene))
			{
				newOpened = rid;
			}
		}

		if (oldOpened != newOpened)
		{
			sceneEditor->ClearSelection(nullptr);

			sceneEditor->m_editorScene = {};

			if (newOpened)
			{
				if (Resources::GetType(newOpened)->GetID() == TypeInfo<EntityResource>::ID())
				{
					sceneEditor->m_editorScene = std::make_shared<Scene>(TypedRID<EntityResource>(newOpened), true);
				}
				else if (Resources::GetType(newOpened)->GetID() == TypeInfo<SceneResource>::ID())
				{
					sceneEditor->m_editorScene = std::make_shared<Scene>(newOpened, true);
				}
				else
				{
					logger.Warn("Invalid type selected");
				}
			}
		}
	}

	void SceneEditor::OnSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		SceneEditor* sceneEditor = static_cast<SceneEditor*>(userData);

		if (newValue.GetUInt(SceneEditorSelection::Workspace) != sceneEditor->m_workspace.GetId())
		{
			return;
		}

		if (oldValue && sceneEditor->m_selection == oldValue.GetRID())
		{
			for (RID deselected : oldValue.GetReferenceArray(SceneEditorSelection::SelectedEntities))
			{
				onEntityDeselectionHandler.Invoke(sceneEditor->m_workspace.GetId(), deselected);

				if (Entity* entity = sceneEditor->m_editorScene->FindEntityByRID(deselected))
				{
					sceneEditor->m_selectionCache.Erase(entity);
					sceneEditor->m_selectionCacheByRID.Erase(deselected);
				}
			}
		}

		if (newValue && sceneEditor->m_selection == newValue.GetRID())
		{
			for (RID selected : newValue.GetReferenceArray(SceneEditorSelection::SelectedEntities))
			{
				if (Entity* entity = sceneEditor->m_editorScene->FindEntityByRID(selected))
				{
					sceneEditor->m_selectionCache.Insert(entity);
					sceneEditor->m_selectionCacheByRID.Insert(selected, entity);
				}
				onEntitySelectionHandler.Invoke(sceneEditor->m_workspace.GetId(), selected);
			}
		}
	}

	void SceneEditor::OnEntityChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		SceneEditor* sceneEditor = static_cast<SceneEditor*>(userData);
		if (oldValue && !newValue)
		{
			if (auto it = sceneEditor->m_selectionCacheByRID.Find(oldValue.GetRID()))
			{
				sceneEditor->m_selectionCache.Erase(it->second);
				sceneEditor->m_selectionCacheByRID.Erase(it);
			}
		}
	}

	void SceneEditor::ClearSelection(UndoRedoScope* scope)
	{
		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);
		selectionObject.Commit(scope);
	}

	void SceneEditor::ClearDebugEntitySelection()
	{
		for (Entity* entity : m_selectedEntities)
		{
			onEntityDebugDeselectionHandler.Invoke(m_workspace.GetId(), entity);
		}
		m_selectedEntities.Clear();
	}


	void RegisterSceneEditorTypes()
	{
		Resources::Type<SceneEditorSelection>()
			.Field<SceneEditorSelection::Workspace>(ResourceFieldType::UInt)
			.Field<SceneEditorSelection::SelectedEntities>(ResourceFieldType::ReferenceArray)
			.Build();

		Resources::Type<SceneEditorState>()
			.Field<SceneEditorState::Workspace>(ResourceFieldType::UInt)
			.Field<SceneEditorState::OpenEntity>(ResourceFieldType::Reference)
			.Field<SceneEditorState::OpenScene>(ResourceFieldType::Reference)
			.Build();
	}
}
