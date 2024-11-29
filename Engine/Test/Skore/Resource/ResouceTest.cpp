#include <doctest.h>

#include "Skore/Engine.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Repository.hpp"
#include "Skore/Resource/ResourceTypes.hpp"
#include "Skore/Scene/Component/TransformComponent.hpp"

using namespace Skore;

namespace
{
    TEST_CASE("Skore::Resource::Basics")
    {
        Engine::Init();
        {
            RID rid = Repository::CreateObjectOf<TransformComponent>();
            ResourceObject obj = Repository::Write(rid);
            obj.SetVec3(obj.GetIndex("position"), Vec3{});
            obj.Commit();
        }
        Engine::Destroy();
    }
}
