#include "Skore/Selection.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/EntityTracker.hpp"
#include "Skore/Scene/Components/UIDocument.hpp"
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
		bool           hasSelectionUI = false;

		SelectionType cachedType = SelectionType::None;
		Array<RID>    cachedRIDs{};
		RID           cachedActive = {};
		RID           cachedPreview = {};
		u64           selectionRevision = 0;

		struct ResolvedEntityCache
		{
			Scene*         scene = nullptr;
			u64            selectionRevision = 0;
			u64            entityRevision = 0;
			bool           valid = false;
			Array<Entity*> entities{};
		};

		ResolvedEntityCache resolvedEntityCache{};

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

		void InvalidateResolvedEntities()
		{
			++selectionRevision;
			resolvedEntityCache.scene = nullptr;
			resolvedEntityCache.valid = false;
			resolvedEntityCache.entities.Clear();
		}

		void CacheSelectionState(ResourceObject& value)
		{
			cachedType = SelectionType::None;
			cachedRIDs.Clear();
			cachedActive = {};
			cachedPreview = {};

			if (value)
			{
				cachedType = static_cast<SelectionType>(value.GetUInt(SelectionState::Type));
				Span<RID> selected = value.GetReferenceArray(SelectionState::Items);
				cachedRIDs.Assign(selected.begin(), selected.end());
				cachedActive = value.GetReference(SelectionState::Active);
				cachedPreview = value.GetReference(SelectionState::Preview);
			}

			InvalidateResolvedEntities();
		}

		bool IsUIDocumentComponent(RID component)
		{
			if (!component) return false;

			if (ResourceType* type = Resources::GetType(component))
			{
				return type->GetID() == TypeInfo<UIDocument>::ID();
			}
			return false;
		}

		bool EntityResourceIsActive(RID entity)
		{
			RID current = entity;
			while (current)
			{
				ResourceType* type = Resources::GetType(current);
				if (!type)
				{
					return false;
				}

				if (type->GetID() != TypeInfo<EntityResource>::ID())
				{
					return true;
				}

				ResourceObject entityObject = Resources::Read(current);
				if (!entityObject || entityObject.GetBool(EntityResource::Deactivated))
				{
					return false;
				}

				current = Resources::GetParent(current);
			}
			return true;
		}

		bool EntityHasUIDocumentResource(RID entity)
		{
			if (!entity) return false;

			if (ResourceType* type = Resources::GetType(entity))
			{
				if (type->GetID() != TypeInfo<EntityResource>::ID())
				{
					return false;
				}
			}

			if (!EntityResourceIsActive(entity))
			{
				return false;
			}

			ResourceObject entityObject = Resources::Read(entity);
			if (!entityObject)
			{
				return false;
			}

			for (RID component : entityObject.GetSubObjectList(EntityResource::Components))
			{
				if (IsUIDocumentComponent(component))
				{
					return true;
				}
			}
			return false;
		}

		bool SelectionStateHasUI(ResourceObject& value)
		{
			if (!value || static_cast<SelectionType>(value.GetUInt(SelectionState::Type)) != SelectionType::Entity)
			{
				return false;
			}

			for (RID selected : value.GetReferenceArray(SelectionState::Items))
			{
				if (EntityHasUIDocumentResource(selected))
				{
					return true;
				}
			}
			return false;
		}

		bool RuntimeSelectionHasUI()
		{
			for (Entity* entity : runtimeEntities)
			{
				if (EntityTracker::IsAlive(entity) && entity->IsActive() && entity->GetComponent<UIDocument>())
				{
					return true;
				}
			}
			return false;
		}

		void RebuildSelectedEntityResourceUI()
		{
			ResourceObject object = Resources::Read(state);
			if (object && static_cast<SelectionType>(object.GetUInt(SelectionState::Type)) == SelectionType::Entity)
			{
				hasSelectionUI = SelectionStateHasUI(object);
			}
		}

		//entities can be destroyed at any time from other threads, so instead of listening to
		//removal events the selection drops dead pointers lazily before they are read or mutated
		bool PruneDeadRuntime()
		{
			if (runtimeEntities.Empty() && !activeRuntime)
			{
				return false;
			}

			bool pruned = false;
			for (usize i = runtimeEntities.Size(); i > 0; --i)
			{
				if (!EntityTracker::IsAlive(runtimeEntities[i - 1]))
				{
					runtimeEntities.Erase(runtimeEntities.begin() + (i - 1), runtimeEntities.begin() + i);
					pruned = true;
				}
			}

			if (activeRuntime && !EntityTracker::IsAlive(activeRuntime))
			{
				activeRuntime = runtimeEntities.Empty() ? nullptr : runtimeEntities[runtimeEntities.Size() - 1];
				pruned = true;
			}

			if (pruned)
			{
				hasSelectionUI = RuntimeSelectionHasUI();
				InvalidateResolvedEntities();
			}
			return pruned;
		}

		void ClearRuntimeInternal()
		{
			bool changed = !runtimeEntities.Empty() || activeRuntime;
			PruneDeadRuntime();

			if (!runtimeEntities.Empty())
			{
				u32 workspaceId = ActiveWorkspaceId();
				for (Entity* entity : runtimeEntities)
				{
					onEntityDebugDeselectionHandler.Invoke(workspaceId, entity);
				}
				runtimeEntities.Clear();
				changed = true;
			}
			if (activeRuntime)
			{
				changed = true;
				activeRuntime = nullptr;
			}
			hasSelectionUI = false;
			if (changed)
			{
				InvalidateResolvedEntities();
			}
		}

		void OnSelectionStateChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			u32 workspaceId = ActiveWorkspaceId();
			CacheSelectionState(newValue);
			hasSelectionUI = SelectionStateHasUI(newValue);

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

		void OnEntityResourceChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			RebuildSelectedEntityResourceUI();
		}
	}

	void Selection::Init()
	{
		state = Resources::Create<SelectionState>();
		Resources::Write(state).Commit();

		Resources::FindType<SelectionState>()->RegisterEvent(ResourceEventType::Changed, OnSelectionStateChanged, nullptr);
		Resources::FindType<EntityResource>()->RegisterEvent(ResourceEventType::Changed, OnEntityResourceChanged, nullptr);
	}

	void Selection::Shutdown()
	{
		Resources::FindType<EntityResource>()->UnregisterEvent(ResourceEventType::Changed, OnEntityResourceChanged, nullptr);
		Resources::FindType<SelectionState>()->UnregisterEvent(ResourceEventType::Changed, OnSelectionStateChanged, nullptr);

		Resources::Destroy(state);
		state = {};
		runtimeEntities.Clear();
		activeRuntime = nullptr;
		hasSelectionUI = false;
		cachedType = SelectionType::None;
		cachedRIDs.Clear();
		cachedActive = {};
		cachedPreview = {};
		InvalidateResolvedEntities();
	}

	SelectionType Selection::GetType()
	{
		return cachedType;
	}

	bool Selection::Empty()
	{
		PruneDeadRuntime();
		return cachedRIDs.Empty() && runtimeEntities.Empty();
	}

	bool Selection::HasSelectionUI()
	{
		PruneDeadRuntime();
		if (!runtimeEntities.Empty())
		{
			hasSelectionUI = RuntimeSelectionHasUI();
		}
		return hasSelectionUI;
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
		for (RID selected : cachedRIDs)
		{
			if (selected == rid)
			{
				return true;
			}
		}
		return false;
	}

	Span<RID> Selection::GetSelectedRIDs()
	{
		return cachedRIDs;
	}

	RID Selection::GetActiveRID()
	{
		return cachedActive;
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
		return cachedPreview;
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
		hasSelectionUI = RuntimeSelectionHasUI();
		InvalidateResolvedEntities();

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
		hasSelectionUI = RuntimeSelectionHasUI();
		InvalidateResolvedEntities();

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

	Span<Entity*> Selection::ResolveEntities(Scene* scene)
	{
		if (!scene) return {};

		u64 entityRevision = EntityTracker::GetRevision();
		if (resolvedEntityCache.valid &&
			resolvedEntityCache.scene == scene &&
			resolvedEntityCache.selectionRevision == selectionRevision &&
			resolvedEntityCache.entityRevision == entityRevision)
		{
			return resolvedEntityCache.entities;
		}

		if (!runtimeEntities.Empty() || activeRuntime)
		{
			PruneDeadRuntime();
			entityRevision = EntityTracker::GetRevision();
		}

		resolvedEntityCache.entities.Clear();
		resolvedEntityCache.scene = scene;
		resolvedEntityCache.selectionRevision = selectionRevision;
		resolvedEntityCache.entityRevision = entityRevision;
		resolvedEntityCache.valid = true;

		if (!runtimeEntities.Empty())
		{
			resolvedEntityCache.entities.Reserve(runtimeEntities.Size());
			for (Entity* entity : runtimeEntities)
			{
				if (entity->GetScene() == scene)
				{
					resolvedEntityCache.entities.EmplaceBack(entity);
				}
			}
			return resolvedEntityCache.entities;
		}

		if (cachedType == SelectionType::Entity)
		{
			resolvedEntityCache.entities.Reserve(cachedRIDs.Size());
			for (RID rid : cachedRIDs)
			{
				if (Entity* entity = scene->FindEntityByRID(rid))
				{
					resolvedEntityCache.entities.EmplaceBack(entity);
				}
			}
		}
		return resolvedEntityCache.entities;
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
