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

#include "EditorWorkspace.hpp"

#include "Editor.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"


namespace Skore
{
    namespace
    {
        u32 workspaceCount = 0;
        Logger& logger = Logger::GetLogger("Skore::EditorWorkspace");
        EventHandler<OnAssetSelection> onAssetSelectionHandler{};
    }

    EditorWorkspace::EditorWorkspace() : id(workspaceCount++), name(String("Workspace ").Append(ToString(id)))
    {
    }

    EditorWorkspace::~EditorWorkspace()
    {
        //ResourceTypes::RemoveListener(GetTypeID<SceneEditorData>(), SceneChanged, this);
    }

    StringView EditorWorkspace::GetName() const
    {
        return name;
    }

    u32 EditorWorkspace::GetId() const
    {
        return id;
    }

    WorldEditor* EditorWorkspace::GetWorldEditor()
    {
        return &worldEditor;
    }

    void EditorWorkspace::OpenAsset(RID rid)
    {
        //onAssetSelectionHandler.Invoke(assetFile);
    }

    void EditorWorkspace::OpenAsset(AssetFileOld* assetFile)
    {
        onAssetSelectionHandler.Invoke(assetFile);
    }

    SceneEditor* EditorWorkspace::GetSceneEditor()
    {
        return &sceneEditor;
    }

    //    void EditorWorkspace::OpenScene(RID rid, TypeID sceneEditorType)
//    {
//        ScopedTimer scopedTimer(logger, "scene change took: ");
//
//        TypeHandler* handler = Registry::FindTypeById(sceneEditorType);
//        SK_ASSERT(handler, "type not found");
//
//        UndoRedoScope* scope = Editor::CreateUndoRedoScope("Scene Selection");
//        ResourceObject obj = Resources::Write(sceneEditorData);
//        obj.SetUInt(SceneEditorData::SceneEditor, reinterpret_cast<usize>(handler->NewObject(rid)));    //not sure about it
//        obj.Commit(scope);
//    }

//    void EditorWorkspace::SceneChanged(RID rid, ResourceObject oldValue, ResourceObject newValue, VoidPtr userData)
//    {
//        if (newValue)
//        {
//            EditorWorkspace& editorWorkspace = *static_cast<EditorWorkspace*>(userData);
//            editorWorkspace.sceneEditor = static_cast<SceneEditor*>(reinterpret_cast<Object*>(newValue.GetUInt(SceneEditorData::SceneEditor)));
//        }
//    }

    void EditorWorkspace::RegisterType(NativeReflectType<EditorWorkspace>& type)
    {
//        ResourceTypeBuilder::Create<SceneEditorData>()
//            .Field<SceneEditorData::SceneEditor>(ResourceFieldType::UInt)
//            .Build();
    }


}
