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

#include "SceneCommands.hpp"

#include "Skore/EditorWorkspace.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	namespace
	{
		EventHandler<OnEntitySelection>   onEntitySelectionHandler{};
		EventHandler<OnEntityDeselection> onEntityDeselectionHandler{};
	}

	ClearSelectionCommand::ClearSelectionCommand(SceneEditor* sceneEditor) : m_sceneEditor(sceneEditor)
	{
		m_selectedEntities.Clear();
		m_selectedEntities.Reserve(m_sceneEditor->GetSelectedEntities().Size());
		for (UUID selected : m_sceneEditor->GetSelectedEntities())
		{
			m_selectedEntities.EmplaceBack(selected);
		}
	}

	void ClearSelectionCommand::Execute()
	{
		m_sceneEditor->InternalClearSelection();
	}

	void ClearSelectionCommand::Undo()
	{
		EditorWorkspace& workspace = m_sceneEditor->GetWorkspace();

		m_sceneEditor->m_selectedEntities.Clear();
		for (UUID selected : m_selectedEntities)
		{
			m_sceneEditor->m_selectedEntities.Emplace(selected);
			onEntitySelectionHandler.Invoke(workspace.GetId(), selected);
		}
	}

	String ClearSelectionCommand::GetName() const
	{
		return "Clear Selection";
	}

	SelectionCommand::SelectionCommand(SceneEditor* sceneEditor, const HashSet<UUID>& oldSelection, const HashSet<UUID>& newSelection)
		: m_sceneEditor(sceneEditor),
		  m_oldSelection(oldSelection),
		  m_newSelection(newSelection) {}

	void SelectionCommand::Execute()
	{
		EditorWorkspace& workspace = m_sceneEditor->GetWorkspace();

		m_sceneEditor->m_selectedEntities.Clear();
		for (UUID entity : m_newSelection)
		{
			m_sceneEditor->m_selectedEntities.Emplace(entity);
		}

		for (const auto& selectedUUID : m_newSelection)
		{
			if (!m_oldSelection.Has(selectedUUID))
			{
				onEntitySelectionHandler.Invoke(workspace.GetId(), selectedUUID);
			}
		}

		for (const auto& deselectedUUID : m_oldSelection)
		{
			if (!m_newSelection.Has(deselectedUUID))
			{
				onEntityDeselectionHandler.Invoke(workspace.GetId(), deselectedUUID);
			}
		}
	}

	void SelectionCommand::Undo()
	{
		EditorWorkspace& workspace = m_sceneEditor->GetWorkspace();

		m_sceneEditor->m_selectedEntities.Clear();
		for (UUID entity : m_oldSelection)
		{
			m_sceneEditor->m_selectedEntities.Emplace(entity);
		}

		for (const auto& selectedUUID : m_oldSelection)
		{
			if (!m_oldSelection.Has(selectedUUID))
			{
				onEntitySelectionHandler.Invoke(workspace.GetId(), selectedUUID);
			}
		}

		for (const auto& deselectedUUID : m_newSelection)
		{
			if (!m_newSelection.Has(deselectedUUID))
			{
				onEntityDeselectionHandler.Invoke(workspace.GetId(), deselectedUUID);
			}
		}
	}

	String SelectionCommand::GetName() const
	{
		return "Select Entity";
	}

	CreateEntityCommand::CreateEntityCommand(SceneEditor* sceneEditor, Entity* parent, StringView name):
		m_sceneEditor(sceneEditor), m_parent(parent->GetUUID()),
		m_name(name)
	{
		m_createdEntityUUID = UUID::RandomUUID();
	}

	void CreateEntityCommand::Execute()
	{
		EditorWorkspace& workspace = m_sceneEditor->GetWorkspace();
		if (Entity* parent = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_parent))
		{
			Entity::Instantiate(parent, m_name, m_createdEntityUUID);
			m_sceneEditor->m_selectedEntities.Insert(m_createdEntityUUID);
			onEntitySelectionHandler.Invoke(workspace.GetId(), m_createdEntityUUID);
			m_sceneEditor->MarkDirty();
		}
	}

	void CreateEntityCommand::Undo()
	{
		EditorWorkspace& workspace = m_sceneEditor->GetWorkspace();
		if (Entity* entity = GetCreatedEntity())
		{
			onEntityDeselectionHandler.Invoke(workspace.GetId(), m_createdEntityUUID);
			m_sceneEditor->m_selectedEntities.Erase(m_createdEntityUUID);
			entity->Destroy();
			m_sceneEditor->MarkDirty();
		}
	}

	String CreateEntityCommand::GetName() const
	{
		return "Create Entity Command";
	}

	Entity* CreateEntityCommand::GetCreatedEntity() const
	{
		return m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_createdEntityUUID);
	}

	RenameEntityCommand::RenameEntityCommand(SceneEditor* sceneEditor, Entity* entity, StringView newName)
		: m_sceneEditor(sceneEditor),
		  m_entityUUID(entity->GetUUID()),
		  m_oldName(entity->GetName()),
		  m_newName(newName) {}

	void RenameEntityCommand::Execute()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->SetName(m_newName);
			m_sceneEditor->MarkDirty();
		}
	}

	void RenameEntityCommand::Undo()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->SetName(m_oldName);
			m_sceneEditor->MarkDirty();
		}
	}

	String RenameEntityCommand::GetName() const
	{
		return "Rename Entity, (" + m_oldName + ") to (" + m_newName + ")";
	}

	DestroyEntityCommand::DestroyEntityCommand(SceneEditor* sceneEditor, Entity* entity) : m_sceneEditor(sceneEditor), m_entityUUID(entity->GetUUID()),
	                                                                                       m_entityParentUUID(entity->GetParent() ? entity->GetParent()->GetUUID() : UUID{}),
	                                                                                       m_typeId(entity->GetTypeId()),
	                                                                                       m_entityIndex(entity->GetSiblingIndex()),
	                                                                                       m_name(entity->GetName())
	{
		BinaryArchiveWriter writer;
		entity->SerializeWithChildren(writer);
		m_bytes = writer.GetData();
	}

	void DestroyEntityCommand::Execute()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			m_sceneEditor->InternalDeselectEntity(m_entityUUID);
			entity->Destroy();
			m_sceneEditor->MarkDirty();
		}
	}

	void DestroyEntityCommand::Undo()
	{
		Entity* newEntity = nullptr;

		if (Entity* parent = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityParentUUID))
		{
			newEntity = Entity::Instantiate(parent, m_name, m_entityUUID);
		}
		else if (ReflectType* reflectType = Reflection::FindTypeById(m_typeId))
		{
			newEntity = reflectType->NewObject()->SafeCast<Entity>();
			newEntity->SetName(m_name);
			newEntity->SetUUID(m_entityUUID);
			m_sceneEditor->GetCurrentScene()->SetRootEntity(newEntity);
		}

		if (newEntity)
		{
			BinaryArchiveReader reader(m_bytes);
			newEntity->DeserializeWithChildren(reader);
			newEntity->SetSiblingIndex(m_entityIndex);
			m_sceneEditor->InternalSelectEntity(m_entityUUID);
			m_sceneEditor->MarkDirty();
		}
	}

	String DestroyEntityCommand::GetName() const
	{
		return "Remove Entity";
	}

	AddComponentCommand::AddComponentCommand(SceneEditor* sceneEditor, Entity* entity, TypeID typeId)
		: m_sceneEditor(sceneEditor), m_entityUUID(entity->GetUUID()), m_typeId(typeId) {}

	void AddComponentCommand::Execute()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->AddComponent(m_typeId);
			m_sceneEditor->MarkDirty();
		}
	}

	void AddComponentCommand::Undo()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->RemoveComponent(m_typeId);
			m_sceneEditor->MarkDirty();
		}
	}

	String AddComponentCommand::GetName() const
	{
		return "Add Component";
	}

	RemoveComponentCommand::RemoveComponentCommand(SceneEditor* sceneEditor, Entity* entity, Component2* component)
		: m_sceneEditor(sceneEditor), m_entityUUID(entity->GetUUID()), m_typeId(component->GetTypeId()),
		  m_componentIndex(entity->GetComponentIndex(component))
	{
		BinaryArchiveWriter writer;
		component->Serialize(writer);
		m_bytes = writer.GetData();
	}

	void RemoveComponentCommand::Execute()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->RemoveComponentAt(m_componentIndex);
			m_sceneEditor->MarkDirty();
		}
	}

	void RemoveComponentCommand::Undo()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			Component2* component = entity->AddComponent(m_typeId);

			BinaryArchiveReader reader(m_bytes);
			component->Deserialize(reader);

			entity->MoveComponentTo(component, m_componentIndex);
			m_sceneEditor->MarkDirty();
		}
	}

	String RemoveComponentCommand::GetName() const
	{
		return "Remove Component";
	}

	EntityMoveCommand::EntityMoveCommand(SceneEditor* sceneEditor, Entity* entity, const Transform& oldTransform, const Transform& newTransform) : m_sceneEditor(sceneEditor),
		m_entityUUID(entity->GetUUID()),
		m_oldTransform(oldTransform),
		m_newTransform(newTransform)
	{
		entity->SetOverride("transform");
	}


	void EntityMoveCommand::Execute()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->SetTransform(m_newTransform);
			m_sceneEditor->MarkDirty();
		}
	}

	void EntityMoveCommand::Undo()
	{
		if (Entity* entity = m_sceneEditor->GetCurrentScene()->FindEntityByUUID(m_entityUUID))
		{
			entity->SetTransform(m_oldTransform);
			m_sceneEditor->MarkDirty();
		}
	}

	String EntityMoveCommand::GetName() const
	{
		return "Move Entity";
	}


	UpdateComponentCommand::UpdateComponentCommand(SceneEditor* sceneEditor, Entity* entity, Component2* oldValue, Component2* newValue)
	{

	}
}
