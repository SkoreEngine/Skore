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
#include "UndoRedoSystem.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Scene/Component2.hpp"
#include "Skore/Scene/SceneEditor.hpp"

namespace Skore
{
	class ClearSelectionCommand : public Command
	{
	public:
		ClearSelectionCommand(SceneEditor* sceneEditor);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor* m_sceneEditor = nullptr;
		Array<UUID>  m_selectedEntities;
	};

	class SelectionCommand : public Command
	{
	public:
		SelectionCommand(SceneEditor* sceneEditor, const HashSet<UUID>& oldSelection, const HashSet<UUID>& newSelection);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor*  m_sceneEditor = nullptr;
		HashSet<UUID> m_oldSelection;
		HashSet<UUID> m_newSelection;
	};

	class CreateEntityCommand : public Command
	{
	public:
		CreateEntityCommand(SceneEditor* sceneEditor, Entity* parent, StringView name);

		void Execute() override;
		void Undo() override;

		String  GetName() const override;
		Entity* GetCreatedEntity() const;

	private:
		SceneEditor* m_sceneEditor;
		UUID         m_parent;
		String       m_name;
		UUID         m_createdEntityUUID;
	};

	class RenameEntityCommand : public Command
	{
	public:
		RenameEntityCommand(SceneEditor* sceneEditor, Entity* entity, StringView newName);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor* m_sceneEditor;
		UUID         m_entityUUID;
		String       m_oldName;
		String       m_newName;
	};

	class DestroyEntityCommand : public Command
	{
	public:
		DestroyEntityCommand(SceneEditor* sceneEditor, Entity* entity);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor* m_sceneEditor;
		UUID         m_entityUUID;
		UUID         m_entityParentUUID;
		TypeID       m_typeId;
		u64          m_entityIndex;
		String       m_name;
		Array<u8>    m_bytes;
	};

	class AddComponentCommand : public Command
	{
	public:
		AddComponentCommand(SceneEditor* sceneEditor, Entity* entity, TypeID typeId);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor* m_sceneEditor;
		UUID         m_entityUUID;
		TypeID       m_typeId;
	};

	class RemoveComponentCommand : public Command
	{
	public:
		RemoveComponentCommand(SceneEditor* sceneEditor, Entity* entity, Component2* component);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor* m_sceneEditor;
		UUID         m_entityUUID;
		TypeID       m_typeId;
		u32          m_componentIndex;
		Array<u8>    m_bytes;
	};

	class EntityMoveCommand : public Command
	{
	public:
		EntityMoveCommand(SceneEditor* sceneEditor, Entity* entity, const Transform& oldTransform, const Transform& newTransform);

		void   Execute() override;
		void   Undo() override;
		String GetName() const override;

	private:
		SceneEditor* m_sceneEditor;
		UUID         m_entityUUID;
		Transform    m_oldTransform;
		Transform    m_newTransform;
	};

	class UpdateComponentCommand : public Command
	{
	public:
		UpdateComponentCommand(SceneEditor* sceneEditor, Entity* entity, Component2* oldValue, Component2* newValue);
	private:
		SceneEditor* m_sceneEditor;
		UUID         m_entityUUID;
		u32          m_componentIndex;
		Array<u8>    m_oldBytes;
		Array<u8>    m_newBytes;
	};
}
