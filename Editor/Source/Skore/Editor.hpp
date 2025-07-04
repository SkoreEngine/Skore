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

#include <functional>

#include "MenuItem.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	struct UndoRedoScope;
	class EditorWorkspace;
	typedef void (*FnConfirmCallback)(VoidPtr userdata);

	struct Editor
	{
		static void             AddMenuItem(const MenuItemCreation& menuItem);
		static void             OpenWindow(TypeID windowType, VoidPtr initUserData = nullptr);
		static void             ShowConfirmDialog(StringView message, VoidPtr userData, FnConfirmCallback callback);
		static void             ShowErrorDialog(StringView message);
		static EditorWorkspace& GetCurrentWorkspace();
		static UndoRedoScope*   CreateUndoRedoScope(StringView name);
		static void             LockUndoRedo(bool lock);
		static RID              GetProject();
		static Span<RID>        GetPackages();
		static RID              LoadPackage(StringView name, StringView directory);
		static void				ExecuteOnMainThread(std::function<void()> func);

		template <typename T>
		static void OpenWindow(VoidPtr initUserData = nullptr)
		{
			OpenWindow(TypeInfo<T>::ID(), initUserData);
		}
	};
}
