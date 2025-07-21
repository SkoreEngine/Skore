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

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Events.hpp"
#include "Skore/IO/Input.hpp"
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
			SelectedEntities
		};
	};

	struct SceneEditorState
	{
		enum
		{
			OpenEntity
		};
	};

	SceneEditor::SceneEditor(EditorWorkspace& workspace) : m_workspace(workspace)
	{
		m_state = Resources::Create<SceneEditorState>();
		Resources::Write(m_state).Commit();


		m_selection = Resources::Create<SceneEditorSelection>();
		Resources::Write(m_selection).Commit();

		Resources::FindType<SceneEditorSelection>()->RegisterEvent(OnSelectionChange, this);
		Resources::FindType<SceneEditorState>()->RegisterEvent(OnStateChange, this);

		Event::Bind<OnUpdate, &SceneEditor::OnUpdateEvent>(this);
	}

	SceneEditor::~SceneEditor()
	{
		Resources::FindType<SceneEditorSelection>()->UnregisterEvent(OnSelectionChange, this);
		Resources::FindType<SceneEditorState>()->UnregisterEvent(OnStateChange, this);

		Resources::Destroy(m_selection);
		Resources::Destroy(m_state);

		Event::Unbind<OnUpdate, &SceneEditor::OnUpdateEvent>(this);
	}

	void SceneEditor::OpenEntity(RID entity)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Entity On Editor");
		ResourceObject stateObject = Resources::Write(m_state);
		stateObject.SetReference(SceneEditorState::OpenEntity, entity);
		stateObject.Commit(scope);
	}

	RID SceneEditor::GetRootEntity() const
	{
		ResourceObject stateObject = Resources::Read(m_state);
		return stateObject.GetReference(SceneEditorState::OpenEntity);
	}

	bool SceneEditor::IsReadOnly() const
	{
		//TODO
		return false;
	}

	void SceneEditor::Create()
	{
		CreateFromAsset(RID {});
	}

	void SceneEditor::CreateFromAsset(RID entityAsset, bool addOnSelected, Vec3 position)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Create Entity");
		Span<RID> selectedEntities = GetSelectedEntities();

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(SceneEditorSelection::SelectedEntities);

		auto createEntity = [&](RID parent)
		{
			RID newEntity;

			if (!entityAsset)
			{
				RID transform = Resources::Create<Transform>(UUID::RandomUUID(), scope);
				ResourceObject transformObject = Resources::Write(transform);
				transformObject.SetVec3(Transform::Position, position);
				transformObject.Commit(scope);

				newEntity = Resources::Create<EntityResource>(UUID::RandomUUID(), scope);
				ResourceObject newEntityObject = Resources::Write(newEntity);
				newEntityObject.SetString(EntityResource::Name, "New Entity");
				newEntityObject.SetSubObject(EntityResource::Transform, transform);
				newEntityObject.Commit(scope);
			}
			else
			{
				newEntity = Resources::CreateFromPrototype(entityAsset, UUID::RandomUUID(), scope);

				if (position.x != 0.0f || position.y != 0.0f || position.z != 0.0f)
				{
					RID transform = Resources::Create<Transform>(UUID::RandomUUID(), scope);
					ResourceObject transformObject = Resources::Write(transform);
					transformObject.SetVec3(Transform::Position, position);
					transformObject.Commit(scope);

					ResourceObject newEntityObject = Resources::Write(newEntity);
					newEntityObject.SetSubObject(EntityResource::Transform, transform);
					newEntityObject.Commit(scope);
				}
			}

			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectList(EntityResource::Children, newEntity);
			parentObject.Commit(scope);

			selectionObject.AddToReferenceArray(SceneEditorSelection::SelectedEntities, newEntity);
		};

		if (!addOnSelected || selectedEntities.Empty())
		{
			createEntity(GetRootEntity());
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

			ResourceObject parentObject = Resources::Write(Resources::GetParent(selected));
			parentObject.AddToSubObjectList(EntityResource::Children, newEntity);
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
			ResourceObject oldParent = Resources::Write(Resources::GetParent(selected));
			oldParent.RemoveFromSubObjectList(EntityResource::Children, selected);
			oldParent.Commit(scope);

			ResourceObject newParentObject = Resources::Write(newParent);
			newParentObject.AddToSubObjectList(EntityResource::Children, selected);
			newParentObject.Commit(scope);
		}
	}

	void SceneEditor::MoveSelectedBefore(RID moveTo)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move Entity");

		if (!moveTo)
		{
			ChangeParentOfSelected(GetRootEntity(), scope);
			return;
		}

		RID parent = Resources::GetParent(moveTo);

		Array<RID> entitiesToMove;
		bool moveToIsSelected = false;

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
				parentObject.IterateSubObjectList(EntityResource::Children, [&](RID child)
				{
					if (selectedEntities.Has(child))
					{
						entitiesToMove.EmplaceBack(child);
					}
				});
			}
		}
		usize index = SIZE_MAX;

		if (moveToIsSelected)
		{
			ResourceObject parentObject = Resources::Read(parent);
			Span<RID> children = parentObject.GetSubObjectList(EntityResource::Children);
			index = FindFirstIndex(children.begin(), children.end(), moveTo);
		}

		for (RID selected : entitiesToMove)
		{
			ResourceObject oldParent = Resources::Write(Resources::GetParent(selected));
			oldParent.RemoveFromSubObjectList(EntityResource::Children, selected);
			oldParent.Commit(scope);
		}

		//get index
		if (!moveToIsSelected)
		{
			ResourceObject parentObject = Resources::Read(parent);
			Span<RID> children = parentObject.GetSubObjectList(EntityResource::Children);
			index = FindFirstIndex(children.begin(), children.end(), moveTo);
		}

		if (index == SIZE_MAX)
		{
			logger.Error("something went wrong, index not found");
		}

		ResourceObject newParentObject = Resources::Write(parent);
		newParentObject.AddToSubObjectListAt(EntityResource::Children, entitiesToMove, index);
		newParentObject.Commit(scope);
	}

	void SceneEditor::AddBackToThisInstance(RID entity, RID prototype)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Back To This Instance");

		RID newInstance = Resources::CreateFromPrototype(prototype, UUID::RandomUUID(), scope);

		ResourceObject entityObject = Resources::Write(entity);
		entityObject.AddToSubObjectList(EntityResource::Children, newInstance);
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


		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Select Entity");
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
		for (RID selected: selectionObject.GetReferenceArray(SceneEditorSelection::SelectedEntities))
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
		RID component = Resources::Create(componentId, UUID::RandomUUID());
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
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.RemoveFromSubObjectList(EntityResource::Components, component);
		entityObject.Commit(scope);
	}

	void SceneEditor::MoveComponentTo(RID component, u32 newIndex)
	{
		RID entity = Resources::GetParent(component);
		{
			ResourceObject entityObject = Resources::Read(entity);
			Span<RID> components = entityObject.GetSubObjectList(EntityResource::Components);
			u32 currentIndex = FindFirstIndex(components.begin(), components.end(), component);
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

	void SceneEditor::OnUpdateEvent()
	{
		if (!m_simulationScene && m_shouldStartSimulation)
		{
			m_simulationScene = std::make_shared<Scene>(GetRootEntity(), true);
			SceneManager::SetActiveScene(m_simulationScene.get());
			m_shouldStartSimulation = false;
		}

		if (m_simulationScene && m_shouldStopSimulation)
		{
			m_simulationScene = {};
			SceneManager::SetActiveScene(nullptr);
			m_shouldStopSimulation = false;
		}

		if (SceneManager::GetActiveScene() == nullptr && m_editorScene)
		{
			m_editorScene->ExecuteEvents();
		}
	}

	const HashSet<Entity*>& SceneEditor::GetSelectionCache() const
	{
		return m_selectionCache;
	}


	void SceneEditor::OnStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		SceneEditor* sceneEditor = static_cast<SceneEditor*>(userData);

		RID oldEntity{};
		RID newEntity{};

		if (oldValue)
		{
			oldEntity = oldValue.GetReference(SceneEditorState::OpenEntity);
		}

		if (newValue)
		{
			newEntity = newValue.GetReference(SceneEditorState::OpenEntity);
		}

		if (oldEntity != newEntity)
		{
			sceneEditor->ClearSelection(nullptr);

			sceneEditor->m_editorScene = {};

			if (newEntity)
			{
				sceneEditor->m_editorScene = std::make_shared<Scene>(newEntity, true);
			}
		}
	}

	void SceneEditor::OnSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		SceneEditor* sceneEditor = static_cast<SceneEditor*>(userData);

		if (oldValue && sceneEditor->m_selection == oldValue.GetRID())
		{
			for (RID deselected : oldValue.GetReferenceArray(SceneEditorSelection::SelectedEntities))
			{
				onEntityDeselectionHandler.Invoke(sceneEditor->m_workspace.GetId(), deselected);

				if (Entity* entity = sceneEditor->m_editorScene->FindEntityByRID(deselected))
				{
					sceneEditor->m_selectionCache.Erase(entity);
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
				}
				onEntitySelectionHandler.Invoke(sceneEditor->m_workspace.GetId(), selected);
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
		for (Entity* entity: m_selectedEntities)
		{
			onEntityDebugDeselectionHandler.Invoke(m_workspace.GetId(), entity);
		}
		m_selectedEntities.Clear();
	}


	void RegisterSceneEditorTypes()
	{
		Resources::Type<SceneEditorSelection>()
			.Field<SceneEditorSelection::SelectedEntities>(ResourceFieldType::ReferenceArray)
			.Build();

		Resources::Type<SceneEditorState>()
			.Field<SceneEditorState::OpenEntity>(ResourceFieldType::Reference)
			.Build();
	}
}
