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

#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/Commands/UndoRedoSystem.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Graphics/BasicSceneRenderer.hpp"
#include "Skore/Scene/Entity.hpp"

namespace Skore
{
	class Component2;
}

namespace Skore
{
	class EditorWorkspace;
	class AssetFileOld;
	class Scene;
	class Entity;
}

namespace Skore
{
	class SceneEditor
	{
	public:
		SceneEditor(EditorWorkspace& workspace);
		~SceneEditor();

		EditorWorkspace& GetWorkspace() const;

		void   OpenScene(AssetFileOld* assetFile);
		bool   IsLoaded() const;
		bool   IsReadOnly() const;
		Scene* GetCurrentScene() const;

		Entity* GetRoot() const;
		void Create();

		void AddComponent(Entity* entity, TypeID componentTypeID);
		void RemoveComponent(Entity* entity, Component2* component);
		void ResetComponent(Entity* entity, Component2* component);

		void DeleteSelected();

		void    DuplicateSelected();
		Entity* Duplicate(Entity* entity);

		void ChangeParent(Entity* newParent, const Array<Entity*>& objects);

		//selection
		bool IsSelected(UUID entity) const;
		void SelectEntity(UUID entity, bool clearSelection = false);
		void DeselectEntity(UUID entity);
		void SetSelectedEntities(Span<UUID> entities);
		bool HasSelectedEntities() const;
		void ClearSelection();
		bool IsParentOfSelected(Entity* entity) const;

		const HashSet<UUID>& GetSelectedEntities() const;

		void Rename(Entity* entity, StringView newName);

		void UpdateTransform(Entity* entity, const Transform& oldTransform, const Transform& newTransform);

		bool IsLocked(Entity* entity);
		void SetLocked(Entity* entity, bool locked);
		void SetActive(Entity* entity, bool active);

		bool IsSimulating() const;
		void StartSimulation();
		void StopSimulation();

		void MarkDirty() const;

		Ref<Transaction> BeginSceneTransaction(StringView name) const;

		friend class SelectionCommand;
		friend class ClearSelectionCommand;
		friend class CreateEntityCommand;
		friend class DestroyEntityCommand;
	private:
		EditorWorkspace& m_workspace;
		AssetFileOld*       m_assetFile = nullptr;
		Scene*           m_scene = nullptr;
		Scene*           m_simulationScene = nullptr;
		HashSet<UUID>    m_selectedEntities;
		HashSet<UUID>    m_lockedEntities;
		bool             m_isSimulating = false;

		void UpdateSelection(const HashSet<UUID>& oldSelection, const HashSet<UUID>& newSelection);

		void InternalClearSelection();
		void InternalDeselectEntity(UUID entityUUID);
		void InternalSelectEntity(UUID entityUUID);

		void DoUpdate() const;
	};
}
