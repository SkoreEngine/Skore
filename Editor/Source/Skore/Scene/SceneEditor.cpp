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
#include "Skore/Asset/AssetFile.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Commands/UndoRedoSystem.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/SceneAsset.hpp"
#include "Skore/Scene/SceneManager.hpp"

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
		Event::Unbind<OnUpdate, &SceneEditor::DoUpdate>(this);
	}

	EditorWorkspace& SceneEditor::GetWorkspace() const
	{
		return m_workspace;
	}

	void SceneEditor::DoUpdate() const
	{
		// if (m_simulationScene)
		// {
		// 	m_simulationScene->Update(App::DeltaTime());
		// }
		// else if (m_scene)
		// {
		// 	m_scene->FlushQueues();
		// }
	}


	void SceneEditor::OpenScene(AssetFile* assetFile)
	{
		//TODO:Reset undo redo
		StopSimulation();
		ClearSelection();
		m_lockedEntities.Clear();

		m_assetFile = assetFile;
		m_sceneAsset = assetFile->GetInstance()->Cast<SceneAsset>();

		m_scene = Alloc<Scene>();
		m_scene->SetRootEntity(m_sceneAsset->Instantiate());

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
		if (Scene* activeScene = SceneManager::GetActiveScene())
		{
			return activeScene;
		}
		return m_scene;
	}

	Entity* SceneEditor::GetRoot() const
	{
		if (Scene* scene = GetCurrentScene())
		{
			return scene->GetRootEntity();
		}
		return nullptr;
	}

	bool SceneEditor::IsSelected(UUID entityUUID) const
	{
		if (!entityUUID)
		{
			return false;
		}

		return m_selectedEntities.Has(entityUUID);
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

		for (UUID selected : m_selectedEntities)
		{
			onEntityDeselectionHandler.Invoke(m_workspace.GetId(), selected);
		}

		m_selectedEntities.Clear();
	}

	bool SceneEditor::IsParentOfSelected(Entity* entity) const
	{
		if (entity == nullptr) return false;

		for (const UUID& selectedUUID : m_selectedEntities)
		{
			// Entity* selected = GetCurrentScene()->FindEntityByUUID(selectedUUID);
			// if (selected && selected->IsChildOf(entity))
			// {
			// 	return true;
			// }
		}
		return false;
	}

	void SceneEditor::Create()
	{
		Array<UUID> selectedEntities;
		if (!m_selectedEntities.Empty())
		{
			for (UUID selected : m_selectedEntities)
			{
				if (Entity* parent = Entity::FindByUUID(selected))
				{
					Entity* entity = Entity::Instantiate(UUID::RandomUUID(), "Entity");
					parent->AddChild(entity);

					selectedEntities.EmplaceBack(entity->GetUUID());
				}
			}
		}
		else
		{
			Entity* entity = Entity::Instantiate(UUID::RandomUUID(), "Entity");
			m_scene->GetRootEntity()->AddChild(entity);
			selectedEntities.EmplaceBack(entity->GetUUID());
		}

		ClearSelection();

		for (UUID entityUUID : selectedEntities)
		{
			SelectEntity(entityUUID);
		}

		MarkDirty();
	}

	void SceneEditor::AddComponent(Entity* entity, TypeID componentTypeID)
	{
		entity->AddComponent(componentTypeID);
		MarkDirty();
	}

	void SceneEditor::RemoveComponent(Entity* entity, Component* component)
	{

	}

	void SceneEditor::ResetComponent(Entity* entity, Component* component)
	{

	}

	void SceneEditor::DeleteSelected()
	{

	}

	void SceneEditor::DuplicateSelected()
	{

	}

	Entity* SceneEditor::Duplicate(Entity* entity)
	{
		return nullptr;
	}

	void SceneEditor::ChangeParent(Entity* newParent, const Array<Entity*>& objects)
	{

	}

	void SceneEditor::SelectEntity(UUID entityUUID, bool clearSelection)
	{
		if (clearSelection)
		{
			ClearSelection();
		}

		if (!m_selectedEntities.Has(entityUUID))
		{
			m_selectedEntities.Insert(entityUUID);
			onEntitySelectionHandler.Invoke(m_workspace.GetId(), entityUUID);
		}
	}

	void SceneEditor::DeselectEntity(UUID entityUUID)
	{
		if (const auto& it = m_selectedEntities.Find(entityUUID))
		{
			onEntityDeselectionHandler.Invoke(m_workspace.GetId(), *it);
			m_selectedEntities.Erase(it);
		}
	}

	void SceneEditor::Rename(Entity* entity, StringView newName)
	{

	}

	void SceneEditor::UpdateTransform(Entity* entity, const Transform& oldTransform, const Transform& newTransform)
	{

	}

	bool SceneEditor::IsLocked(Entity* entity)
	{
		return false;
	}

	void SceneEditor::SetLocked(Entity* entity, bool locked)
	{

	}

	void SceneEditor::SetActive(Entity* entity, bool active)
	{

	}

	bool SceneEditor::IsSimulating() const
	{
		return m_isSimulating;
	}

	void SceneEditor::StartSimulation()
	{

	}

	void SceneEditor::StopSimulation()
	{
	}

	void SceneEditor::MarkDirty() const
	{
		if (m_assetFile)
		{
			m_assetFile->MarkDirty();
		}
	}
}
