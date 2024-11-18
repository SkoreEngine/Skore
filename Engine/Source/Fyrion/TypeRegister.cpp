#include "TypeRegister.hpp"

#include "Core/Registry.hpp"
#include "Graphics/RenderProxy.hpp"
#include "Physics/PhysicsProxy.hpp"

namespace Fyrion
{
    void RegisterCoreTypes();
    void RegisterIOTypes();
    void RegisterGraphicsTypes();
    void RegisterSceneTypes();
    void RegisterPhysicsTypes();

    void TypeRegister()
    {
        RegisterCoreTypes();
        RegisterIOTypes();
        RegisterGraphicsTypes();
        RegisterSceneTypes();
        RegisterPhysicsTypes();

        //defer registers
        Registry::Type<RenderProxy>();
        Registry::Type<PhysicsProxy>();
    }
}
