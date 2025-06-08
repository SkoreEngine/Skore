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

#include "Skore/Common.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class EditorWorkspace;

	class SK_API WorldEditor
	{
	public:
		WorldEditor(EditorWorkspace& workspace);
		~WorldEditor();

		void OpenEntity(RID entity);

		RID  GetRootEntity() const;
		bool IsReadOnly() const;

		//entity management
		void Create();
		void DestroySelected();
		void DuplicateSelected();

		//selection
		void ClearSelection();
		void SelectEntity(RID entity, bool clearSelection);
		bool IsSelected(RID entity);
		bool IsParentOfSelected(RID entity);
		bool HasSelectedEntities() const;
		Span<RID> GetSelectedEntities() const;

		//actions
		void SetActivated(RID entity, bool activated);
		void SetLocked(RID entity, bool locked);
		void Rename(RID entity, StringView newName);

		//components
		void AddComponent(RID entity, TypeID componentId);
		void ResetComponent(RID entity, RID component);
		void RemoveComponent(RID entity, RID component);

	private:
		EditorWorkspace& m_workspace;
		RID m_state = {};
		RID m_selection = {};

		static void OnSelectionChange(const ResourceObject& oldValue, const ResourceObject& newValue, VoidPtr userData);
	};
}
