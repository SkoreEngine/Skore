#pragma once

#include <functional>

#include "Skore/Common.hpp"
#include "MenuItem.hpp"
#include "Action/EditorAction.hpp"
#include "Asset/AssetEditor.hpp"

namespace Skore
{
    class SceneEditor;
}

namespace Skore::Editor
{
    SK_API void               Init(StringView projectFile);
    SK_API void               AddMenuItem(const MenuItemCreation& menuItem);
    SK_API void               OpenWindow(TypeID windowType, VoidPtr initUserData = nullptr);
    SK_API EditorTransaction* CreateTransaction();
    SK_API String             CreateProject(StringView newProjectPath, StringView projectName);
    SK_API SceneEditor&       GetSceneEditor();

    SK_API void               ExecuteOnMainThread(std::function<void()> func);


    template <typename T>
    SK_API void OpenWindow(VoidPtr initUserData = nullptr)
    {
        OpenWindow(GetTypeID<T>(), initUserData);
    }
}
