#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
  class Scene;

  class SK_API PreviewGenerator
  {
  public:
    virtual ~PreviewGenerator() = default;
    virtual void SetupScene(Scene* scene) = 0;

    virtual f32 PercentageInScreen()
    {
      return 0.70f;
    }

    void PopulateScene(Scene* scene);
    void GenerateThumbnail(RID asset);

    static void SetupDefaultEnvironment(Scene* scene);
  };
}
