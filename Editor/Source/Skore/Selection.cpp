#include "Skore/Selection.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/EntityTracker.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	struct SelectionState
	{
		enum
		{
			Type,
			Items,
			Active,
			Preview
		};
	};

	namespace
	{
		RID            state = {};
		Array<Entity*> runtimeEntities{};
		Entity*        activeRuntime = nullptr;

		EventHandler<OnSelectionChanged>       onSelectionChangedHandler{};
		EventHandler<OnEntitySelection>        onEntitySelectionHandler{};
		EventHandler<OnEntityDeselection>      onEntityDeselectionHandler{};
		EventHandler<OnEntityDebugSelection>   onEntityDebugSelectionHandler{};
		EventHandler<OnEntityDebugDeselection> onEntityDebugDeselectionHandler{};

		u32 ActiveWorkspaceId()
		{
			if (EditorWorkspace* workspace = Editor::GetActiveWorkspace())
			{
				return workspace->GetId();
			}
			return 0;
		}

		bool RuntimeContains(Entity* entity)
		{
			for (Entity* current : runtimeEntities)
			{
				if (current == entity) return true;
			}
			return false;
		}

		bool RuntimeErase(Entity* entity)
		{
			for (usize i = 0; i < runtimeEntities.Size(); ++i)
			{
				if (runtimeEntities[i] == entity)
				{
					runtimeEntities.Erase(runtimeEntities.begin() + i, runtimeEntities.begin() + i + 1);
					return true;
				}
			}
			return false;
		}

		//entities can be destroyed at any time from other threads, so instead of listening to
		//removal events the selection drops dead pointers lazily before they are read or mutated
		void PruneDeadRuntime()
		{
			for (usize i = runtimeEntities.Size(); i > 0; --i)
			{
				if (!EntityTracker::IsAlive(runtimeEntities[i - 1]))
				{
					runtimeEntities.Erase(runtimeEntities.begin() + (i - 1), runtimeEntities.begin() + i);
				}
			}

			if (activeRuntime && !EntityTracker::IsAlive(activeRuntime))
			{
				activeRuntime = runtimeEntities.Empty() ? nullptr : runtimeEntities[runtimeEntities.Size() - 1];
			}
		}

		void ClearRuntimeInternal()
		{
			PruneDeadRuntime();

			if (!runtimeEntities.Empty())
			{
				u32 workspaceId = ActiveWorkspaceId();
				for (Entity* entity : runtimeEntities)
				{
					onEntityDebugDeselectionHandler.Invoke(workspaceId, entity);
				}
				runtimeEntities.Clear();
			}
			activeRuntime = nullptr;
		}

		void OnSelectionStateChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			u32 workspaceId = ActiveWorkspaceId();

			if (oldValue && static_cast<SelectionType>(oldValue.GetUInt(SelectionState::Type)) == SelectionType::Entity)
			{
				for (RID deselected : oldValue.GetReferenceArray(SelectionState::Items))
				{
					onEntityDeselectionHandler.Invoke(workspaceId, deselected);
				}
			}

			if (newValue && static_cast<SelectionType>(newValue.GetUInt(SelectionState::Type)) == SelectionType::Entity)
			{
				for (RID selected : newValue.GetReferenceArray(SelectionState::Items))
				{
					onEntitySelectionHandler.Invoke(workspaceId, selected);
				}
			}

			onSelectionChangedHandler.Invoke();
		}
	}

	void Selection::Init()
	{
		state = Resources::Create<SelectionState>();
		Resources::Write(state).Commit();

		Resources::FindType<SelectionState>()->RegisterEvent(ResourceEventType::Changed, OnSelectionStateChanged, nullptr);
	}

	void Selection::Shutdown()
	{
		Resources::FindType<SelectionState>()->UnregisterEvent(ResourceEventType::Changed, OnSelectionStateChanged, nullptr);

		Resources::Destroy(state);
		state = {};
		runtimeEntities.Clear();
		activeRuntime = nullptr;
	}

	SelectionType Selection::GetType()
	{
		ResourceObject object = Resources::Read(state);
		return static_cast<SelectionType>(object.GetUInt(SelectionState::Type));
	}

	bool Selection::Empty()
	{
		PruneDeadRuntime();
		return GetSelectedRIDs().Empty() && runtimeEntities.Empty();
	}

	void Selection::Clear(UndoRedoScope* scope)
	{
		ClearRuntimeInternal();

		ResourceObject object = Resources::Write(state);
		object.SetUInt(SelectionState::Type, static_cast<u64>(SelectionType::None));
		object.ClearReferenceArray(SelectionState::Items);
		object.SetReference(SelectionState::Active, {});
		object.SetReference(SelectionState::Preview, {});
		object.Commit(scope);
	}

	void Selection::Select(SelectionType type, RID rid, bool clearSelection, UndoRedoScope* scope)
	{
		if (!rid) return;

		ClearRuntimeInternal();

		ResourceObject object = Resources::Write(state);
		bool typeChanged = static_cast<SelectionType>(object.GetUInt(SelectionState::Type)) != type;
		if (clearSelection || typeChanged)
		{
			object.ClearReferenceArray(SelectionState::Items);
			object.SetReference(SelectionState::Preview, {});
		}
		object.SetUInt(SelectionState::Type, static_cast<u64>(type));
		object.AddToReferenceArray(SelectionState::Items, rid);
		object.SetReference(SelectionState::Active, rid);
		object.Commit(scope);
	}

	void Selection::Select(SelectionType type, Span<RID> rids, bool clearSelection, UndoRedoScope* scope)
	{
		ClearRuntimeInternal();

		ResourceObject object = Resources::Write(state);
		bool typeChanged = static_cast<SelectionType>(object.GetUInt(SelectionState::Type)) != type;
		if (clearSelection || typeChanged)
		{
			object.ClearReferenceArray(SelectionState::Items);
			object.SetReference(SelectionState::Preview, {});
		}
		object.SetUInt(SelectionState::Type, static_cast<u64>(type));

		RID active = {};
		for (RID rid : rids)
		{
			if (rid)
			{
				object.AddToReferenceArray(SelectionState::Items, rid);
				active = rid;
			}
		}
		if (active)
		{
			object.SetReference(SelectionState::Active, active);
		}
		object.Commit(scope);
	}

	void Selection::Deselect(RID rid, UndoRedoScope* scope)
	{
		if (!rid) return;

		ResourceObject object = Resources::Write(state);
		object.RemoveFromReferenceArray(SelectionState::Items, rid);
		if (object.GetReference(SelectionState::Active) == rid)
		{
			object.SetReference(SelectionState::Active, {});
		}
		object.Commit(scope);
	}

	bool Selection::IsSelected(RID rid)
	{
		if (!rid) return false;
		ResourceObject object = Resources::Read(state);
		return object.HasOnReferenceArray(SelectionState::Items, rid);
	}

	Span<RID> Selection::GetSelectedRIDs()
	{
		ResourceObject object = Resources::Read(state);
		return object.GetReferenceArray(SelectionState::Items);
	}

	RID Selection::GetActiveRID()
	{
		ResourceObject object = Resources::Read(state);
		return object.GetReference(SelectionState::Active);
	}

	void Selection::SelectAsset(RID asset, RID preview, UndoRedoScope* scope)
	{
		if (!asset) return;

		ClearRuntimeInternal();

		ResourceObject object = Resources::Write(state);
		object.SetUInt(SelectionState::Type, static_cast<u64>(SelectionType::Asset));
		object.ClearReferenceArray(SelectionState::Items);
		object.AddToReferenceArray(SelectionState::Items, asset);
		object.SetReference(SelectionState::Active, asset);
		object.SetReference(SelectionState::Preview, preview);
		object.Commit(scope);
	}

	RID Selection::GetPreview()
	{
		ResourceObject object = Resources::Read(state);
		return object.GetReference(SelectionState::Preview);
	}

	void Selection::Select(Entity* entity, bool clearSelection)
	{
		if (!EntityTracker::IsAlive(entity)) return;

		PruneDeadRuntime();

		{
			ResourceObject object = Resources::Write(state);
			object.SetUInt(SelectionState::Type, static_cast<u64>(SelectionType::RuntimeEntity));
			object.ClearReferenceArray(SelectionState::Items);
			object.SetReference(SelectionState::Active, {});
			object.SetReference(SelectionState::Preview, {});
			object.Commit();
		}

		if (clearSelection)
		{
			ClearRuntimeInternal();
		}

		if (!RuntimeContains(entity))
		{
			runtimeEntities.EmplaceBack(entity);
		}
		activeRuntime = entity;

		onEntityDebugSelectionHandler.Invoke(ActiveWorkspaceId(), entity);
		onSelectionChangedHandler.Invoke();
	}

	void Selection::Deselect(Entity* entity)
	{
		if (!entity) return;

		PruneDeadRuntime();
		if (!RuntimeErase(entity)) return;

		if (activeRuntime == entity)
		{
			activeRuntime = runtimeEntities.Empty() ? nullptr : runtimeEntities[runtimeEntities.Size() - 1];
		}

		onEntityDebugDeselectionHandler.Invoke(ActiveWorkspaceId(), entity);
		onSelectionChangedHandler.Invoke();
	}

	bool Selection::IsSelected(Entity* entity)
	{
		PruneDeadRuntime();
		return RuntimeContains(entity);
	}

	Span<Entity*> Selection::GetSelectedEntities()
	{
		PruneDeadRuntime();
		return runtimeEntities;
	}

	Entity* Selection::GetActiveEntity()
	{
		PruneDeadRuntime();
		return activeRuntime;
	}

	Array<Entity*> Selection::ResolveEntities(Scene* scene)
	{
		Array<Entity*> result;
		if (!scene) return result;

		PruneDeadRuntime();

		if (!runtimeEntities.Empty())
		{
			for (Entity* entity : runtimeEntities)
			{
				if (entity->GetScene() == scene)
				{
					result.EmplaceBack(entity);
				}
			}
			return result;
		}

		if (GetType() == SelectionType::Entity)
		{
			ResourceObject object = Resources::Read(state);
			for (RID rid : object.GetReferenceArray(SelectionState::Items))
			{
				if (Entity* entity = scene->FindEntityByRID(rid))
				{
					result.EmplaceBack(entity);
				}
			}
		}
		return result;
	}

	void Selection::RegisterType()
	{
		Resources::Type<SelectionState>()
			.Field<SelectionState::Type>(ResourceFieldType::UInt)
			.Field<SelectionState::Items>(ResourceFieldType::ReferenceArray)
			.Field<SelectionState::Active>(ResourceFieldType::Reference)
			.Field<SelectionState::Preview>(ResourceFieldType::Reference)
			.Build();
	}
}
