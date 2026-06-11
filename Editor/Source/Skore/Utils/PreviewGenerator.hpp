#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Object.hpp"

namespace Skore
{
  class Scene;

  class SK_API PreviewGenerator : public Object
  {
  public:
    SK_CLASS(PreviewGenerator, Object);

    virtual ~PreviewGenerator() = default;
    virtual void SetupScene(Scene* scene) = 0;

    virtual f32 PercentageInScreen()
    {
      return 0.70f;
    }

    void         PopulateScene(Scene* scene);
    virtual void GenerateThumbnail();

    static void SetupDefaultEnvironment(Scene* scene);

    //the asset the thumbnail is being generated for; set internally before GenerateThumbnail.
    RID asset;
  };
}
