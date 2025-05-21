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

#include "UndoRedoSystem.hpp"

#include "imgui.h"
#include "Skore/Core/Allocator.hpp"

namespace Skore
{
	Transaction::Transaction(StringView name) : m_name(name) {}

	Transaction::~Transaction()
	{
		for (Command* command : m_commands)
		{
			DestroyAndFree(command);
		}
		m_commands.Clear();
	}

	void Transaction::AddCommand(Command* command)
	{
		m_commands.EmplaceBack(command);
	}

	void Transaction::Execute()
	{
		if (m_executed)
		{
			return;
		}

		for (Command* command : m_commands)
		{
			command->Execute();
		}

		m_executed = true;
	}

	void Transaction::Undo()
	{
		if (!m_executed)
		{
			return;
		}

		for (i32 i = static_cast<i32>(m_commands.Size()) - 1; i >= 0; i--)
		{
			m_commands[i]->Undo();
		}

		m_executed = false;
	}

	String Transaction::GetName() const
	{
		return m_name;
	}

	bool Transaction::IsEmpty() const
	{
		return m_commands.Empty();
	}

	// Static member initialization
	Array<Ref<Transaction>> UndoRedoSystem::s_undoStack;
	Array<Ref<Transaction>> UndoRedoSystem::s_redoStack;

	void UndoRedoSystem::Initialize()
	{
		Clear();
	}

	void UndoRedoSystem::Shutdown()
	{
		Clear();
	}

	void UndoRedoSystem::Clear()
	{
		s_undoStack.Clear();
		s_redoStack.Clear();
	}

	Ref<Transaction> UndoRedoSystem::BeginTransaction(u32 category, StringView name)
	{
		return Ref<Transaction>(Alloc<Transaction>(name));
	}

	void UndoRedoSystem::EndTransaction(const Ref<Transaction>& transaction)
	{
		if (!transaction || transaction->IsEmpty())
		{
			return;
		}

		transaction->Execute();
		AddTransaction(transaction);
	}

	void UndoRedoSystem::Undo()
	{
		if (ImGui::GetIO().WantTextInput) return;

		if (!CanUndo())
		{
			return;
		}

		Ref<Transaction> transaction = s_undoStack.Back();
		s_undoStack.PopBack();

		transaction->Undo();
		s_redoStack.EmplaceBack(transaction);
	}

	void UndoRedoSystem::Redo()
	{
		if (ImGui::GetIO().WantTextInput) return;

		if (!CanRedo())
		{
			return;
		}

		Ref<Transaction> transaction = s_redoStack.Back();
		s_redoStack.PopBack();

		transaction->Execute();
		s_undoStack.EmplaceBack(transaction);
	}

	bool UndoRedoSystem::CanUndo()
	{
		return !s_undoStack.Empty();
	}

	bool UndoRedoSystem::CanRedo()
	{
		return !s_redoStack.Empty();
	}

	String UndoRedoSystem::GetUndoName()
	{
		if (CanUndo())
		{
			return "Undo " + s_undoStack.Back()->GetName();
		}
		return "Nothing to Undo";
	}

	String UndoRedoSystem::GetRedoName()
	{
		if (CanRedo())
		{
			return "Redo " + s_redoStack.Back()->GetName();
		}
		return "Nothing to Redo";
	}

	const Array<Ref<Transaction>>& UndoRedoSystem::GetUndoStack()
	{
		return s_undoStack;
	}

	const Array<Ref<Transaction>>& UndoRedoSystem::GetRedoStack()
	{
		return s_redoStack;
	}

	void UndoRedoSystem::AddTransaction(const Ref<Transaction>& transaction)
	{
		s_undoStack.EmplaceBack(transaction);
		s_redoStack.Clear();
	}
}
