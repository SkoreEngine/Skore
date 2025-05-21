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

#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Ref.hpp"

namespace Skore
{

	struct TransactionCategory
	{
		enum
		{
			Assets = 100,
			Entity = 110,
			Simulation = 120,
		};
	};

	class Command
	{
	public:
		virtual ~Command() = default;

		virtual void   Execute() = 0;
		virtual void   Undo() = 0;
		virtual String GetName() const = 0;
	};

	class Transaction
	{
	public:
		explicit Transaction(StringView name);
		~Transaction();

		void AddCommand(Command* command);
		void Execute();
		void Undo();

		String GetName() const;
		bool   IsEmpty() const;

	private:
		String          m_name;
		Array<Command*> m_commands;
		bool            m_executed = false;
	};

	class UndoRedoSystem
	{
	public:
		static void Initialize();
		static void Shutdown();
		static void Clear();

		static Ref<Transaction> BeginTransaction(u32 category, StringView name);
		static void             EndTransaction(const Ref<Transaction>& transaction);

		static void Undo();
		static void Redo();

		static bool CanUndo();
		static bool CanRedo();

		static String GetUndoName();
		static String GetRedoName();

		// Expose stacks for UI visualization
		static const Array<Ref<Transaction>>& GetUndoStack();
		static const Array<Ref<Transaction>>& GetRedoStack();

	private:
		static Array<Ref<Transaction>> s_undoStack;
		static Array<Ref<Transaction>> s_redoStack;

		static void AddTransaction(const Ref<Transaction>& transaction);
	};
}
