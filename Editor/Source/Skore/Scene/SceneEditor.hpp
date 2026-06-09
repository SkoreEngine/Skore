#pragma once

#include "Skore/Common.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	class EditorWorkspace;


	enum class EntityCreationType
	{
		Empty,
		Cube,
		Sphere,
		Cylinder,
		Capsule,
		Plane,
		Sprite2D,
	};


	class SK_API SceneEditor
	{
	public:
		SceneEditor(EditorWorkspace& workspace);
		~SceneEditor();

		void OpenEntity(RID entity);
		void OpenScene(RID scene);

		RID  GetOpenedResource() const;
		bool IsOpenedScene() const;
		bool IsReadOnly() const;

		static u32 GetChildrenField(RID parent);

		//entity management
		void Create(EntityCreationType type, RID entityAsset = {});
		RID  CreateFromAsset(RID parent, RID entityAsset, UndoRedoScope* scope);
		RID  Clone(RID parent, RID entityAsset, UndoRedoScope* scope);

		void DestroySelected();
		void DuplicateSelected();
		void ChangeParentOfSelected(RID newParent);
		void MoveSelectedBefore(RID moveTo);
		void AddBackToThisInstance(RID entity, RID prototype);

		//selection
		void      ClearSelection();
		void      SelectEntity(RID entity, bool clearSelection);
		void      SelectEntity(RID entity, bool clearSelection, UndoRedoScope* scope);
		void      SelectEntities(Span<RID> entities, bool clearSelection);
		void      DeselectEntity(RID entity);
		bool      IsSelected(RID entity);
		bool      IsParentOfSelected(RID entity);
		bool      HasSelectedEntities() const;
		Span<RID> GetSelectedEntities() const;

		//selection for Scene/Entity
		void SelectEntity(Entity* entity, bool clearSelection);
		bool IsSelected(Entity* entity) const;

		//actions
		void SetActivated(RID entity, bool activated);
		void SetLocked(RID entity, bool locked);
		void Rename(RID entity, StringView newName);

		//components
		void AddComponent(RID entity, TypeID componentId);
		void ResetComponent(RID entity, RID component);
		void RemoveComponent(RID entity, RID component);
		void MoveComponentTo(RID component, u32 newIndex);

		//simulation
		bool IsSimulationRunning() const;
		void StartSimulation();
		void StopSimulation();
		void PauseSimulation();

		Scene* GetCurrentScene() const;

		void DoUpdate();

	private:
		EditorWorkspace& m_workspace;
		RID              m_state = {};

		std::shared_ptr<Scene> m_editorScene;
		std::shared_ptr<Scene> m_simulationScene;

		bool m_shouldStartSimulation = false;
		bool m_shouldStopSimulation = false;

		static void OnStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);

		void ClearSelection(UndoRedoScope* scope);

		void ChangeParentOfSelected(RID newParent, UndoRedoScope* scope);
	};
}
