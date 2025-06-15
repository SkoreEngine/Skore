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

#include "WorldEditor.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/World/WorldCommon.hpp"

namespace Skore
{

	static Logger& logger = Logger::GetLogger("Skore::WorldEditor");

	static EventHandler<OnEntityRIDSelection>   onEntitySelectionHandler{};
	static EventHandler<OnEntityRIDDeselection> onEntityDeselectionHandler{};


	struct WorldEditorSelection
	{
		enum
		{
			SelectedEntities
		};
	};

	struct WorldEditorState
	{
		enum
		{
			OpenEntity
		};
	};

	WorldEditor::WorldEditor(EditorWorkspace& workspace) : m_workspace(workspace)
	{
		m_state = Resources::Create<WorldEditorState>();
		Resources::Write(m_state).Commit();


		m_selection = Resources::Create<WorldEditorSelection>();
		Resources::Write(m_selection).Commit();

		Resources::FindType<WorldEditorSelection>()->RegisterEvent(OnSelectionChange, this);
		Resources::FindType<WorldEditorState>()->RegisterEvent(OnStateChange, this);
	}

	WorldEditor::~WorldEditor()
	{
		Resources::Destroy(m_selection);
		Resources::Destroy(m_state);

		Resources::FindType<WorldEditorSelection>()->UnregisterEvent(OnSelectionChange, this);
		Resources::FindType<WorldEditorState>()->UnregisterEvent(OnStateChange, this);
	}

	void WorldEditor::OpenEntity(RID entity)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Entity On Editor");
		ResourceObject stateObject = Resources::Write(m_state);
		stateObject.SetReference(WorldEditorState::OpenEntity, entity);
		stateObject.Commit(scope);
	}

	RID WorldEditor::GetRootEntity() const
	{
		ResourceObject stateObject = Resources::Read(m_state);
		return stateObject.GetReference(WorldEditorState::OpenEntity);
	}

	bool WorldEditor::IsReadOnly() const
	{
		//TODO
		return false;
	}

	void WorldEditor::Create()
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Create Entity");
		Span<RID> selectedEntities = GetSelectedEntities();

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(WorldEditorSelection::SelectedEntities);

		auto createEntity = [&](RID parent)
		{
			RID newEntity = Resources::Create<EntityResource>(UUID::RandomUUID());
			ResourceObject newEntityObject = Resources::Write(newEntity);
			newEntityObject.SetString(EntityResource::Name, "New Entity");
			newEntityObject.Commit(scope);


			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectSet(EntityResource::Children, newEntity);
			parentObject.Commit(scope);

			selectionObject.AddToReferenceArray(WorldEditorSelection::SelectedEntities, newEntity);
		};

		if (selectedEntities.Empty())
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

	void WorldEditor::DestroySelected()
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Destroy Entity");
		for (RID selected : GetSelectedEntities())
		{
			Resources::Destroy(selected, scope);
		}
	}

	void WorldEditor::DuplicateSelected()
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Destroy Entity");

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(WorldEditorSelection::SelectedEntities);

		for (RID selected : GetSelectedEntities())
		{
			RID newEntity = Resources::Clone(selected, UUID::RandomUUID(), scope);

			ResourceObject parentObject = Resources::Write(Resources::GetParent(selected));
			parentObject.AddToSubObjectSet(EntityResource::Children, newEntity);
			parentObject.Commit(scope);

			selectionObject.AddToReferenceArray(WorldEditorSelection::SelectedEntities, newEntity);
		}
		selectionObject.Commit(scope);
	}

	void WorldEditor::ClearSelection()
	{
		ClearSelection(Editor::CreateUndoRedoScope("Clear selection"));
	}

	void WorldEditor::SelectEntity(RID entity, bool clearSelection)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Select Entity");
		ResourceObject selectionObject = Resources::Write(m_selection);
		if (clearSelection)
		{
			selectionObject.ClearReferenceArray(WorldEditorSelection::SelectedEntities);
		}
		selectionObject.AddToReferenceArray(WorldEditorSelection::SelectedEntities, entity);
		selectionObject.Commit(scope);
	}

	bool WorldEditor::IsSelected(RID entity)
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return selectionObject.HasOnReferenceArray(WorldEditorSelection::SelectedEntities, entity);
	}

	bool WorldEditor::IsParentOfSelected(RID entity)
	{
		return false;
	}

	bool WorldEditor::HasSelectedEntities() const
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return !selectionObject.GetReferenceArray(WorldEditorSelection::SelectedEntities).Empty();
	}

	Span<RID> WorldEditor::GetSelectedEntities() const
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return selectionObject.GetReferenceArray(WorldEditorSelection::SelectedEntities);
	}

	void WorldEditor::SetActivated(RID entity, bool activated)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Activate Entity");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.SetBool(EntityResource::Deactivated, !activated);
		entityObject.Commit(scope);
	}

	void WorldEditor::SetLocked(RID entity, bool locked)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Lock Entity");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.SetBool(EntityResource::Locked, locked);
		entityObject.Commit(scope);
	}

	void WorldEditor::Rename(RID entity, StringView newName)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Rename Entity");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.SetString(EntityResource::Name, newName);
		entityObject.Commit(scope);
	}

	void WorldEditor::AddComponent(RID entity, TypeID componentId)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Component");
		RID component = Resources::Create(componentId, UUID::RandomUUID());
		Resources::Write(component).Commit(scope);

		ResourceObject entityObject = Resources::Write(entity);
		entityObject.AddToSubObjectSet(EntityResource::Components, component);
		entityObject.Commit(scope);
	}

	void WorldEditor::ResetComponent(RID entity, RID component)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Reset Component");
		Resources::Reset(component, scope);
	}

	void WorldEditor::RemoveComponent(RID entity, RID component)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Remove Component");
		ResourceObject entityObject = Resources::Write(entity);
		entityObject.RemoveFromSubObjectSet(EntityResource::Components, component);
		entityObject.Commit(scope);
	}

	bool WorldEditor::IsSimulationRunning() const
	{
		return false;
	}

	void WorldEditor::StartSimulation()
	{
		//TODO
	}

	void WorldEditor::StopSimulation()
	{
		//TODO
	}

	void WorldEditor::PauseSimulation()
	{
		//TODO
	}

	World* WorldEditor::GetCurrentWorld() const
	{
		if (m_editorWorld)
		{
			return m_editorWorld.get();
		}
		return nullptr;
	}


	void WorldEditor::OnStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		WorldEditor* worldEditor = static_cast<WorldEditor*>(userData);

		RID oldEntity{};
		RID newEntity{};

		if (oldValue)
		{
			oldEntity = oldValue.GetReference(WorldEditorState::OpenEntity);
		}

		if (newValue)
		{
			newEntity = newValue.GetReference(WorldEditorState::OpenEntity);
		}

		if (oldEntity != newEntity)
		{
			worldEditor->ClearSelection(nullptr);

			worldEditor->m_editorWorld = {};

			if (newEntity)
			{
				worldEditor->m_editorWorld = std::make_shared<World>(newEntity, true);
			}
		}
	}

	void WorldEditor::OnSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		WorldEditor* worldEditor = static_cast<WorldEditor*>(userData);

		if (oldValue && worldEditor->m_selection == oldValue.GetRID())
		{
			for (RID deselected : oldValue.GetReferenceArray(WorldEditorSelection::SelectedEntities))
			{
				onEntityDeselectionHandler.Invoke(worldEditor->m_workspace.GetId(), deselected);
			}
		}

		if (newValue && worldEditor->m_selection == newValue.GetRID())
		{
			for (RID selected : newValue.GetReferenceArray(WorldEditorSelection::SelectedEntities))
			{
				onEntitySelectionHandler.Invoke(worldEditor->m_workspace.GetId(), selected);
			}
		}
	}

	void WorldEditor::ClearSelection(UndoRedoScope* scope)
	{
		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(WorldEditorSelection::SelectedEntities);
		selectionObject.Commit(scope);
	}


	void RegisterWorldEditorTypes()
	{
		Resources::Type<WorldEditorSelection>()
			.Field<WorldEditorSelection::SelectedEntities>(ResourceFieldType::ReferenceArray)
			.Build();

		Resources::Type<WorldEditorState>()
			.Field<WorldEditorState::OpenEntity>(ResourceFieldType::Reference)
			.Build();
	}
}
