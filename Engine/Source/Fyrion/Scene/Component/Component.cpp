
#include "Component.hpp"
#include "Fyrion/Scene/GameObject.hpp"

#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Scene/Scene.hpp"


namespace Fyrion
{
    Component::~Component()
    {
        DisableUpdate();
    }

    void Component::EnableUpdate()
    {
        if (!updateEnabled)
        {
            gameObject->GetScene()->componentsToUpdate.emplace(this);
        }
        updateEnabled = true;
    }

    void Component::DisableUpdate()
    {
        if (updateEnabled)
        {
            gameObject->GetScene()->componentsToUpdate.erase(this);
        }
        updateEnabled = false;
    }

    void Component::RegisterType(NativeTypeHandler<Component>& type)
    {
    }
}
