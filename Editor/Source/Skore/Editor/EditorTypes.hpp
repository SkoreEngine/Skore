#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/UUID.hpp"


#define SK_ASSET_PAYLOAD "fy-asset-payload"
#define SK_GAME_OBJECT_PAYLOAD "fy-game-object-payload"


namespace Skore
{
    struct AssetFile;
    class GameObject;

    using OnGameObjectSelection = EventType<"Skore::Editor::OnGameObjectSelection"_h, void(UUID)>;
    using OnGameObjectDeselection = EventType<"Skore::Editor::OnGameObjectDeselection"_h, void(UUID)>;
    using OnAssetSelection = EventType<"Skore::Editor::OnAssetSelection"_h, void(AssetFile*)>;

    enum class DockPosition
    {
        None        = 0,
        Center      = 1,
        Left        = 2,
        TopRight    = 3,
        BottomRight = 4,
        Bottom      = 5
    };

    enum class PlayMode
    {
        Editing    = 0,
        Paused     = 1,
        Simulating = 2
    };

    struct EditorWindowProperties
    {
        DockPosition dockPosition{};
        bool         createOnInit{};
    };


    struct EditorWindow
    {
        virtual void Init(u32 id, VoidPtr userData) {}
        virtual void Draw(u32 id, bool& open) = 0;
        virtual      ~EditorWindow() = default;
    };

    struct AssetPayload
    {
        AssetFile* assetFile;
        TypeID     assetType;
    };

    struct GameObjectPayload
    {
        Span<GameObject*> objects;
    };

    struct EditorPreferences{};
}
