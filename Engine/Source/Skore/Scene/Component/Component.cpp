
#include "Component.hpp"
#include "Skore/Scene/GameObject.hpp"

#include "Skore/Core/Registry.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
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
