#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

#define SK_ASSET_PAYLOAD "sk-asset-payload"
#define SK_ENTITY_PAYLOAD "sk-entity-payload"
#define SK_IMPORT_EXTENSION ".import"
#define SK_INFO_EXTENSION ".info"
#define SK_ASSET_EXTENSION ".asset"
#define SK_PROJECT_EXTENSION ".skore"

namespace Skore {
	class GPUCommandBuffer;
	class ReflectType;
}

namespace Skore
{
	class Entity;
	class EditorWorkspace;

	using OnEntityDebugSelection = EventType<"Skore::Editor::OnEntityDebugSelection"_h, void(u32, Entity*)>;
	using OnEntityDebugDeselection = EventType<"Skore::Editor::OnEntityDebugDeselection"_h, void(u32, Entity*)>;

	using OnEntitySelection = EventType<"Skore::Editor::OnEntitySelection"_h, void(u32, RID)>;
	using OnEntityDeselection = EventType<"Skore::Editor::OnEntityDeselection"_h, void(u32, RID)>;

	using OnAssetSelection = EventType<"Skore::Editor::OnAssetSelection"_h, void(u32, RID)>;
	using OnResourceSelection = EventType<"Skore::Editor::OnResourceSelection"_h, void(u32, RID)>;

	struct SceneViewPipelineModule;


	//mesh or entity thumbnails are generated before finishing texture importing
	//to enable it, needs to wait on render.
	constexpr static bool ImportChildAssetsAsync = false;

	enum class DockPosition
	{
		None        = 0,
		Center      = 1,
		Left        = 2,
		RightTop    = 3,
		RightBottom = 4,
		BottomLeft  = 5,
		BottomRight = 6
	};

	enum class PlayMode
	{
		Editing    = 0,
		Paused     = 1,
		Simulating = 2
	};

	namespace WorkspaceTypes
	{
		constexpr u8 All = U8_MAX;
		constexpr u8 Scene = 1;
		constexpr u8 Graph = 2;
		constexpr u8 Animator = 3;
	}

	struct EditorWorkspaceTypeDesc
	{
		u8         id{};
		StringView displayName{};
		i32        order = 0;
	};

	struct EditorWindowProperties
	{
		DockPosition dockPosition{};
		i32          order = 0;
		Array<u8>    workspaceTypes{};
	};

	struct EditorWindow : Object
	{
		SK_CLASS(EditorWindow, Object);

		EditorWorkspace* workspace = nullptr;
		u32              id = 0;
		bool             skipFocusOnFirstAppearance = false;

		virtual void        Init(VoidPtr userData) {}
		virtual const char* GetTitle() const = 0;
		virtual void        Draw(bool& open) = 0;
		virtual void        Render(GPUCommandBuffer* cmd) {}
	};

	typedef void (*FnWindowIterationCallback)(EditorWindow* window, VoidPtr userdata);

	struct EditorWindowStorage
	{
		TypeID        typeId{};
		DockPosition  dockPosition{};
		i32           order;
		Array<u8>     workspaceTypes{};
	};

	struct OpenWindowStorage
	{
		u32           id{};
		EditorWindow* instance{};
		ReflectType*  reflectType{};
	};

	Span<EditorWindowStorage>      GetEditorWindowStorages();
	Span<EditorWorkspaceTypeDesc>  GetEditorWorkspaceTypeStorages();
	u32                            AllocateWindowId();

	constexpr static Extent thumbnailSize = Extent{128, 128};

	struct AssetPayload
	{
		RID asset;
		RID windowObjectRID;
	};
}
