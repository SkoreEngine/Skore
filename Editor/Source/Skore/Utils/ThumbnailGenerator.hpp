#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
  class Scene;

  class SK_API ThumbnailGenerator
  {
  public:
    virtual ~ThumbnailGenerator() = default;
    virtual void SetupScene(Scene* scene) = 0;

    virtual f32 PercentageInScreen()
    {
      return 0.70f;
    }

    void GenerateThumbnail(RID asset);
  };
}
