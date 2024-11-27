#pragma once

#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"

#define SK_SHADOW_MAP_CASCADE_COUNT 4
#define SK_SHADOW_MAP_DIM 4096


namespace Skore
{
    struct ShadowMapDataInfo
    {
        f32  cascadeSplit[SK_SHADOW_MAP_CASCADE_COUNT];
        Mat4 cascadeViewProjMat[SK_SHADOW_MAP_CASCADE_COUNT];
    };

    struct ShaderLight
    {
        Vec4 directionType;
        Vec4 positionMultiplier;
        Vec4 color;
        Vec4 rangeCutoff;

        void SetDirection(Vec3 direction)
        {
            direction = Math::Normalize(direction);
            directionType.x = direction.x;
            directionType.y = direction.y;
            directionType.z = direction.z;
        }

        void SetType(LightType type)
        {
            directionType.w = static_cast<f32>(type);
        }

        void SetPosition(Vec3 position)
        {
            positionMultiplier.x = position.x;
            positionMultiplier.y = position.y;
            positionMultiplier.z = position.z;
        }

        void SetIndirectMultiplier(f32 multiplier)
        {
            positionMultiplier.w = multiplier;
        }

        void SetColor(const Vec3 color)
        {
            this->color.x = color.x;
            this->color.y = color.y;
            this->color.z = color.z;
        }

        void SetRange(f32 range)
        {
            rangeCutoff.x = range;
        }

        void SetInnerCutoff(f32 value)
        {
            rangeCutoff.y = value;
        }

        void SetOuterCutoff(f32 value)
        {
            rangeCutoff.z = value;
        }

    };

    struct LightingData
    {
        Vec4        cascadeSplits;
        Mat4        cascadeViewProjMat[SK_SHADOW_MAP_CASCADE_COUNT];
        Mat4        viewProjInverse = {};
        Mat4        view = {};
        Vec4        data0 = {};
        Vec4        data1 = {};
        ShaderLight lights[128];

        void SetViewPos(Vec3 viewPos)
        {
            data0.x = viewPos.x;
            data0.y = viewPos.y;
            data0.z = viewPos.z;
        }

        void SetLightCount(i32 count)
        {
            data0.w = static_cast<f32>(count);
        }
    };
}
