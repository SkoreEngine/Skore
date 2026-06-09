#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class Entity;
	class Scene;
	struct UndoRedoScope;

	enum class SelectionType : u8
	{
		None          = 0,
		Asset         = 1,
		Entity        = 2,
		RuntimeEntity = 3,
		Resource      = 4,
	};

	struct SelectionItem
	{
		SelectionType type   = SelectionType::None;
		RID           rid    = {};
		Entity*       entity = nullptr;
	};

	struct SK_API Selection
	{
		static void Init();
		static void Shutdown();
		static void RegisterType();

		static SelectionType GetType();
		static bool          Empty();
		static void          Clear(UndoRedoScope* scope = nullptr);

		static void      Select(SelectionType type, RID rid, bool clearSelection, UndoRedoScope* scope = nullptr);
		static void      Select(SelectionType type, Span<RID> rids, bool clearSelection, UndoRedoScope* scope = nullptr);
		static void      Deselect(RID rid, UndoRedoScope* scope = nullptr);
		static bool      IsSelected(RID rid);
		static Span<RID> GetSelectedRIDs();
		static RID       GetActiveRID();

		static void SelectAsset(RID asset, RID preview, UndoRedoScope* scope = nullptr);
		static RID  GetPreview();

		static void          Select(Entity* entity, bool clearSelection);
		static void          Deselect(Entity* entity);
		static bool          IsSelected(Entity* entity);
		static Span<Entity*> GetSelectedEntities();
		static Entity*       GetActiveEntity();

		static Array<Entity*> ResolveEntities(Scene* scene);
	};
}
