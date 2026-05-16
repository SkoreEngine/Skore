#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Resource/ResourceCommon.hpp"


namespace Skore
{
	class EditorWorkspace;
	struct UndoRedoScope;

	class AnimatorEditor
	{
	public:
		explicit AnimatorEditor(EditorWorkspace& workspace);
		~AnimatorEditor();

		void OpenAnimationController(RID animationController);

		RID       GetController() const;
		RID       GetSelectedLayer() const;
		Span<RID> GetSelectedStates() const;
		Span<RID> GetSelectedTransitions() const;

		// Layer management
		RID  AddLayer(UndoRedoScope* scope = nullptr);
		void RemoveLayer(RID layer, UndoRedoScope* scope = nullptr);
		void RenameLayer(RID layer, StringView newName, UndoRedoScope* scope = nullptr);
		void SelectLayer(RID layer, UndoRedoScope* scope = nullptr);

		// Parameter management
		RID  AddParameter(UndoRedoScope* scope = nullptr);
		void RemoveParameter(RID parameter, UndoRedoScope* scope = nullptr);
		void RenameParameter(RID parameter, StringView newName, UndoRedoScope* scope = nullptr);

		// Avatar management
		RID  AddAvatar(UndoRedoScope* scope = nullptr);
		void RemoveAvatar(RID avatar, UndoRedoScope* scope = nullptr);
		void RenameAvatar(RID avatar, StringView newName, UndoRedoScope* scope = nullptr);

		// State management
		void AddState(RID layer, Vec2 position, UndoRedoScope* scope = nullptr);
		void RemoveState(RID layer, RID state, UndoRedoScope* scope = nullptr);
		void DuplicateState(RID layer, RID state, UndoRedoScope* scope = nullptr);

		// Default state
		void SetDefaultState(RID layer, RID state, UndoRedoScope* scope = nullptr);

		// Transition management
		void AddTransition(RID layer, RID from, RID to, UndoRedoScope* scope = nullptr);
		void RemoveTransition(RID layer, RID transition, UndoRedoScope* scope = nullptr);

		// Selection
		void SetSelection(Span<RID> states, Span<RID> transitions, UndoRedoScope* scope = nullptr);
		void ClearSelection(UndoRedoScope* scope = nullptr);

	private:
		EditorWorkspace& m_workspace;

		RID m_state = {};
		RID m_selection = {};

		static void OnStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
		static void OnSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
	};
}
