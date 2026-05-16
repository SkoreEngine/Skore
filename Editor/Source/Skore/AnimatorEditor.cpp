#include "Skore/AnimatorEditor.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::AnimatorEditor");
	}

	struct AnimatorEditorState
	{
		enum
		{
			Workspace,
			Controller,
			SelectedLayer
		};
	};

	struct AnimatorEditorSelection
	{
		enum
		{
			Workspace,
			SelectedStates,
			SelectedTransitions
		};
	};

	AnimatorEditor::AnimatorEditor(EditorWorkspace& workspace) : m_workspace(workspace)
	{
		{
			m_state = Resources::Create<AnimatorEditorState>();
			ResourceObject stateObject = Resources::Write(m_state);
			stateObject.SetUInt(AnimatorEditorState::Workspace, workspace.GetId());
			stateObject.Commit();
		}

		{
			m_selection = Resources::Create<AnimatorEditorSelection>();
			ResourceObject selectionObject = Resources::Write(m_selection);
			selectionObject.SetUInt(AnimatorEditorSelection::Workspace, workspace.GetId());
			selectionObject.Commit();
		}

		Resources::FindType<AnimatorEditorState>()->RegisterEvent(ResourceEventType::Changed, OnStateChange, this);
		Resources::FindType<AnimatorEditorSelection>()->RegisterEvent(ResourceEventType::Changed, OnSelectionChange, this);
	}

	AnimatorEditor::~AnimatorEditor()
	{
		Resources::FindType<AnimatorEditorState>()->UnregisterEvent(ResourceEventType::Changed, OnStateChange, this);
		Resources::FindType<AnimatorEditorSelection>()->UnregisterEvent(ResourceEventType::Changed, OnSelectionChange, this);

		Resources::Destroy(m_selection);
		Resources::Destroy(m_state);
	}

	void AnimatorEditor::OpenAnimationController(RID animationController)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Animation Controller");
		ResourceObject stateObject = Resources::Write(m_state);
		stateObject.SetReference(AnimatorEditorState::Controller, animationController);
		stateObject.Commit(scope);

		// Auto-select the first layer if one exists
		if (ResourceObject controllerObj = Resources::Read(animationController))
		{
			Span<RID> layers = controllerObj.GetSubObjectList(AnimationControllerResource::Layers);
			if (!layers.Empty())
			{
				SelectLayer(layers[0], scope);
			}
		}
	}

	RID AnimatorEditor::GetController() const
	{
		ResourceObject stateObject = Resources::Read(m_state);
		return stateObject.GetReference(AnimatorEditorState::Controller);
	}

	RID AnimatorEditor::GetSelectedLayer() const
	{
		ResourceObject stateObject = Resources::Read(m_state);
		return stateObject.GetReference(AnimatorEditorState::SelectedLayer);
	}

	Span<RID> AnimatorEditor::GetSelectedStates() const
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return selectionObject.GetReferenceArray(AnimatorEditorSelection::SelectedStates);
	}

	Span<RID> AnimatorEditor::GetSelectedTransitions() const
	{
		ResourceObject selectionObject = Resources::Read(m_selection);
		return selectionObject.GetReferenceArray(AnimatorEditorSelection::SelectedTransitions);
	}

	// --- Layer management ---

	RID AnimatorEditor::AddLayer(UndoRedoScope* scope)
	{
		RID controller = GetController();
		if (!controller) return {};

		RID layer = Resources::Create<AnimationLayerResource>(UUID::RandomUUID(), scope);
		{

			ResourceObject controllerObj = Resources::Read(controller);

			ResourceObject layerObj = Resources::Write(layer);
			layerObj.SetString(AnimationLayerResource::Name, "New Layer");
			layerObj.SetFloat(AnimationLayerResource::Weight, controllerObj.GetSubObjectListCount(AnimationControllerResource::Layers) == 0 ? 1.0 : 0.0);
			layerObj.Commit(scope);
		}

		ResourceObject controllerObj = Resources::Write(controller);
		controllerObj.AddToSubObjectList(AnimationControllerResource::Layers, layer);
		controllerObj.Commit(scope);

		SelectLayer(layer, scope);
		return layer;
	}

	void AnimatorEditor::RemoveLayer(RID layer, UndoRedoScope* scope)
	{
		RID controller = GetController();
		if (!controller || !layer) return;


		if (GetSelectedLayer() == layer)
		{
			ResourceObject stateObject = Resources::Write(m_state);
			stateObject.SetReference(AnimatorEditorState::SelectedLayer, RID{});
			stateObject.Commit(scope);
		}

		ResourceObject controllerObj = Resources::Write(controller);
		controllerObj.RemoveFromSubObjectList(AnimationControllerResource::Layers, layer);
		controllerObj.Commit(scope);
	}

	void AnimatorEditor::RenameLayer(RID layer, StringView newName, UndoRedoScope* scope)
	{
		if (!layer) return;

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.SetString(AnimationLayerResource::Name, newName);
		layerObj.Commit(scope);
	}

	void AnimatorEditor::SelectLayer(RID layer, UndoRedoScope* scope)
	{
		if (GetSelectedLayer() == layer) return;

		ResourceObject stateObject = Resources::Write(m_state);
		stateObject.SetReference(AnimatorEditorState::SelectedLayer, layer);
		stateObject.Commit(scope);

		ClearSelection(scope);
	}

	// --- Parameter management ---

	RID AnimatorEditor::AddParameter(UndoRedoScope* scope)
	{
		RID controller = GetController();
		if (!controller) return {};


		RID parameter = Resources::Create<AnimationParameterResource>(UUID::RandomUUID(), scope);
		{
			ResourceObject paramObj = Resources::Write(parameter);
			paramObj.SetString(AnimationParameterResource::Name, "New Parameter");
			paramObj.SetEnum(AnimationParameterResource::Type, AnimationParameterType::Float);
			paramObj.Commit(scope);
		}

		ResourceObject controllerObj = Resources::Write(controller);
		controllerObj.AddToSubObjectList(AnimationControllerResource::Parameters, parameter);
		controllerObj.Commit(scope);
		return parameter;
	}

	void AnimatorEditor::RemoveParameter(RID parameter, UndoRedoScope* scope)
	{
		RID controller = GetController();
		if (!controller || !parameter) return;


		ResourceObject controllerObj = Resources::Write(controller);
		controllerObj.RemoveFromSubObjectList(AnimationControllerResource::Parameters, parameter);
		controllerObj.Commit(scope);
	}

	void AnimatorEditor::RenameParameter(RID parameter, StringView newName, UndoRedoScope* scope)
	{
		if (!parameter) return;

		ResourceObject paramObj = Resources::Write(parameter);
		paramObj.SetString(AnimationParameterResource::Name, newName);
		paramObj.Commit(scope);
	}

	// --- Avatar management ---

	RID AnimatorEditor::AddAvatar(UndoRedoScope* scope)
	{
		RID controller = GetController();
		if (!controller) return {};

		RID avatar = Resources::Create<AnimationAvatarResource>(UUID::RandomUUID(), scope);
		{
			ResourceObject avatarObj = Resources::Write(avatar);
			avatarObj.SetString(AnimationAvatarResource::Name, "New Avatar");
			avatarObj.Commit(scope);
		}

		ResourceObject controllerObj = Resources::Write(controller);
		controllerObj.AddToSubObjectList(AnimationControllerResource::Avatars, avatar);
		controllerObj.Commit(scope);
		return avatar;
	}

	void AnimatorEditor::RemoveAvatar(RID avatar, UndoRedoScope* scope)
	{
		RID controller = GetController();
		if (!controller || !avatar) return;

		ResourceObject controllerObj = Resources::Write(controller);
		controllerObj.RemoveFromSubObjectList(AnimationControllerResource::Avatars, avatar);
		controllerObj.Commit(scope);
	}

	void AnimatorEditor::RenameAvatar(RID avatar, StringView newName, UndoRedoScope* scope)
	{
		if (!avatar) return;

		ResourceObject avatarObj = Resources::Write(avatar);
		avatarObj.SetString(AnimationAvatarResource::Name, newName);
		avatarObj.Commit(scope);
	}

	// --- State management ---

	void AnimatorEditor::AddState(RID layer, Vec2 position, UndoRedoScope* scope)
	{
		if (!layer) return;

		RID state = Resources::Create<AnimationStateResource>(UUID::RandomUUID(), scope);
		{
			ResourceObject stateObj = Resources::Write(state);
			stateObj.SetVec2(AnimationStateResource::Position, position);
			stateObj.SetFloat(AnimationStateResource::Speed, 1.0f);
			stateObj.Commit(scope);
		}

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.AddToSubObjectList(AnimationLayerResource::States, state);
		layerObj.Commit(scope);

		SetSelection(Span<RID>(&state, 1), {}, scope);
	}

	void AnimatorEditor::RemoveState(RID layer, RID state, UndoRedoScope* scope)
	{
		if (!layer || !state) return;

		// Remove transitions that reference this state
		if (ResourceObject layerObj = Resources::Read(layer))
		{
			Array<RID> transitionsToRemove;
			layerObj.IterateSubObjectList(AnimationLayerResource::Transitions, [&](RID transition)
			{
				if (ResourceObject transObj = Resources::Read(transition))
				{
					RID from = transObj.GetReference(AnimationTransitionResource::From);
					RID to = transObj.GetReference(AnimationTransitionResource::To);
					if (from == state || to == state)
					{
						transitionsToRemove.EmplaceBack(transition);
					}
				}
			});

			if (!transitionsToRemove.Empty())
			{
				ResourceObject layerWrite = Resources::Write(layer);
				for (RID t : transitionsToRemove)
				{
					layerWrite.RemoveFromSubObjectList(AnimationLayerResource::Transitions, t);
				}
				layerWrite.Commit(scope);
			}
		}

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.RemoveFromSubObjectList(AnimationLayerResource::States, state);
		layerObj.Commit(scope);
	}

	void AnimatorEditor::DuplicateState(RID layer, RID state, UndoRedoScope* scope)
	{
		if (!layer || !state) return;

		RID newState = Resources::Create<AnimationStateResource>(UUID::RandomUUID(), scope);

		if (ResourceObject srcObj = Resources::Read(state))
		{
			Vec2 srcPos = srcObj.GetVec2(AnimationStateResource::Position);
			ResourceObject dstObj = Resources::Write(newState);
			dstObj.SetReference(AnimationStateResource::Animation, srcObj.GetReference(AnimationStateResource::Animation));
			dstObj.SetVec2(AnimationStateResource::Position, Vec2{srcPos.x + 50.0f, srcPos.y + 50.0f});
			dstObj.SetFloat(AnimationStateResource::Speed, srcObj.GetFloat(AnimationStateResource::Speed));
			dstObj.Commit(scope);
		}

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.AddToSubObjectList(AnimationLayerResource::States, newState);
		layerObj.Commit(scope);

		SetSelection(Span<RID>(&newState, 1), {}, scope);
	}

	// --- Default state ---

	void AnimatorEditor::SetDefaultState(RID layer, RID state, UndoRedoScope* scope)
	{
		if (!layer) return;

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.SetReference(AnimationLayerResource::DefaultState, state);
		layerObj.Commit(scope);
	}

	// --- Transition management ---

	void AnimatorEditor::AddTransition(RID layer, RID from, RID to, UndoRedoScope* scope)
	{
		if (!layer || !from || !to) return;

		RID transition = Resources::Create<AnimationTransitionResource>(UUID::RandomUUID(), scope);
		{
			ResourceObject transObj = Resources::Write(transition);
			transObj.SetReference(AnimationTransitionResource::From, from);
			transObj.SetReference(AnimationTransitionResource::To, to);
			transObj.Commit(scope);
		}

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.AddToSubObjectList(AnimationLayerResource::Transitions, transition);
		layerObj.Commit(scope);
	}

	void AnimatorEditor::RemoveTransition(RID layer, RID transition, UndoRedoScope* scope)
	{
		if (!layer || !transition) return;

		ResourceObject layerObj = Resources::Write(layer);
		layerObj.RemoveFromSubObjectList(AnimationLayerResource::Transitions, transition);
		layerObj.Commit(scope);
	}

	// --- Selection ---

	void AnimatorEditor::SetSelection(Span<RID> states, Span<RID> transitions, UndoRedoScope* scope)
	{

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(AnimatorEditorSelection::SelectedStates);
		selectionObject.ClearReferenceArray(AnimatorEditorSelection::SelectedTransitions);

		for (RID s : states)
		{
			selectionObject.AddToReferenceArray(AnimatorEditorSelection::SelectedStates, s);
		}
		for (RID t : transitions)
		{
			selectionObject.AddToReferenceArray(AnimatorEditorSelection::SelectedTransitions, t);
		}
		selectionObject.Commit(scope);
	}

	void AnimatorEditor::ClearSelection(UndoRedoScope* scope)
	{
		Span<RID> states = GetSelectedStates();
		Span<RID> transitions = GetSelectedTransitions();
		if (!scope && states.Empty() && transitions.Empty()) return;

		ResourceObject selectionObject = Resources::Write(m_selection);
		selectionObject.ClearReferenceArray(AnimatorEditorSelection::SelectedStates);
		selectionObject.ClearReferenceArray(AnimatorEditorSelection::SelectedTransitions);
		selectionObject.Commit(scope);
	}

	// --- Event callbacks ---

	void AnimatorEditor::OnStateChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		AnimatorEditor* editor = static_cast<AnimatorEditor*>(userData);
		if (newValue)
		{
			if (newValue.GetUInt(AnimatorEditorState::Workspace) != editor->m_workspace.GetId())
			{
				return;
			}
			EventHandler<OnResourceSelection>{}.Invoke(editor->m_workspace.GetId(), newValue.GetReference(AnimatorEditorState::SelectedLayer));
		}
	}

	void AnimatorEditor::OnSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		AnimatorEditor* editor = static_cast<AnimatorEditor*>(userData);
		if (newValue)
		{
			if (newValue.GetUInt(AnimatorEditorSelection::Workspace) != editor->m_workspace.GetId())
			{
				return;
			}

			Span<RID> states = newValue.GetReferenceArray(AnimatorEditorSelection::SelectedStates);
			Span<RID> transitions = newValue.GetReferenceArray(AnimatorEditorSelection::SelectedTransitions);

			RID selected = {};
			if (!states.Empty()) selected = states[states.Size() - 1];
			else if (!transitions.Empty()) selected = transitions[transitions.Size() - 1];

			EventHandler<OnResourceSelection>{}.Invoke(editor->m_workspace.GetId(), selected);
		}
	}

	void RegisterAnimatorEditorTypes()
	{
		Resources::Type<AnimatorEditorState>()
			.Field<AnimatorEditorState::Workspace>(ResourceFieldType::UInt)
			.Field<AnimatorEditorState::Controller>(ResourceFieldType::Reference)
			.Field<AnimatorEditorState::SelectedLayer>(ResourceFieldType::Reference)
			.Build();

		Resources::Type<AnimatorEditorSelection>()
			.Field<AnimatorEditorSelection::Workspace>(ResourceFieldType::UInt)
			.Field<AnimatorEditorSelection::SelectedStates>(ResourceFieldType::ReferenceArray)
			.Field<AnimatorEditorSelection::SelectedTransitions>(ResourceFieldType::ReferenceArray)
			.Build();
	}
}
