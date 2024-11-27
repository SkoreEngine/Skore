#include "EditorAction.hpp"

#include "Skore/Core/Registry.hpp"

namespace Skore
{

    EditorTransaction::~EditorTransaction()
    {
        for(const auto& action : actions)
        {
            action.first->Destroy(action.second);
        }
        actions.Clear();
        actions.ShrinkToFit();
    }

    EditorAction* EditorTransaction::CreateAction(TypeID typeId, VoidPtr* params, TypeID* paramTypes, usize paramNum)
    {
        if (TypeHandler* typeHandler = Registry::FindTypeById(typeId))
        {
            if (ConstructorHandler* constructor = typeHandler->FindConstructor(paramTypes, paramNum))
            {
                VoidPtr instance = constructor->NewInstance(MemoryGlobals::GetDefaultAllocator(), params);
                if (EditorAction* editorAction = typeHandler->Cast<EditorAction>(instance))
                {
                    editorAction->transaction = this;
                    actions.EmplaceBack(MakePair(typeHandler, editorAction));
                    return editorAction;
                }
                SK_ASSERT(false, "cast to EditorAction not found");
                typeHandler->Destroy(instance);
            }
            SK_ASSERT(false, "constructor not found");
        }
        SK_ASSERT(false, "type handler not found");
        return nullptr;
    }

    void EditorTransaction::AddAction(TypeID typeId, EditorAction* action)
    {
        action->transaction = this;
        TypeHandler* typeHandler = Registry::FindTypeById(typeId);
        SK_ASSERT(typeHandler, "type handler not found for action");
        if (typeHandler)
        {
            actions.EmplaceBack(MakePair(typeHandler, action));
        }
    }

    void EditorTransaction::AddPreExecute(VoidPtr usarData, PreActionFn actionFn)
    {
        preExecute.EmplaceBack(usarData, actionFn);
    }

    void EditorTransaction::Commit()
    {
        for (auto& it: preExecute)
        {
            it.action(it.userData);
        }

        for(const auto& action : actions)
        {
            action.second->Commit();
        }
    }

    void EditorTransaction::Rollback()
    {
        for (auto& it: preExecute)
        {
            it.action(it.userData);
        }

        for(const auto& action : actions)
        {
            action.second->Rollback();
        }
    }


    void InitEditorAction()
    {
        Registry::Type<EditorTransaction>();
        Registry::Type<EditorAction>();
    }
}
