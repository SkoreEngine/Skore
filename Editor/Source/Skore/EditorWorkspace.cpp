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
#include "Skore/Resource/Resources.hpp"
#include "Window/PropertiesWindow.hpp"


namespace Skore
{
	struct WorkspaceResourceState
	{
		enum
		{
			SelectedAsset
		};
	};

	namespace
	{
		u32     workspaceCount = 0;
		Logger& logger = Logger::GetLogger("Skore::EditorWorkspace");

		EventHandler<OnAssetSelection>   onAssetSelectionHandler{};
	}

	EditorWorkspace::EditorWorkspace() : id(workspaceCount++), name(String("Workspace ").Append(ToString(id)))
	{
		Resources::FindType<WorkspaceResourceState>()->RegisterEvent(WorkspaceStateChanged, this);

		state = Resources::Create<WorkspaceResourceState>();
	}

	EditorWorkspace::~EditorWorkspace()
	{
		Resources::FindType<WorkspaceResourceState>()->UnregisterEvent(WorkspaceStateChanged, this);
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
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Asset");
		ResourceObject stateObject = Resources::Write(state);
		stateObject.SetReference(WorkspaceResourceState::SelectedAsset, rid);
		stateObject.Commit(scope);
	}

	void EditorWorkspace::RegisterType(NativeReflectType<EditorWorkspace>& type)
	{
		Resources::Type<WorkspaceResourceState>()
			.Field<WorkspaceResourceState::SelectedAsset>(ResourceFieldType::Reference)
			.Build();
	}

	void EditorWorkspace::WorkspaceStateChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		EditorWorkspace* workspace = static_cast<EditorWorkspace*>(userData);
		if (newValue)
		{
			onAssetSelectionHandler.Invoke(workspace->id, newValue.GetReference(WorkspaceResourceState::SelectedAsset));
		}
	}
}
