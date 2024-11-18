#include "Physics.hpp"
#include "Jolt/Jolt.hpp"
#include "Jolt/Core/Core.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/Memory.h"
#include <Jolt/RegisterTypes.h>


namespace Fyrion
{

    namespace
    {

    }


    void PhysicsInit()
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

}
