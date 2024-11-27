#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"


namespace Skore
{
    class SK_API FreeViewCamera
    {
    public:
        FreeViewCamera();
        void Process(f64 deltaTime);

        void SetActive(bool p_active)
        {
            active = p_active;
        }

        bool IsActive() const
        {
            return active;
        }

        const Mat4& GetView() const
        {
            return view;
        }

        Vec3 GetPosition() const
        {
            return position;
        }

        Quat GetRotation() const
        {
            return rotation;
        }

        Vec3 GetScale() const
        {
            return scale;
        }

    private:
        Vec3 position{0, 0, 0};
        Quat rotation{0, 0, 0, 1};
        Vec3 scale{1, 1, 1};

        f32  cameraSpeed = 20.0f;
        f32  yaw{};
        f32  pitch{};
        f32  lastX = 0;
        f32  lastY = 0;
        bool firstMouse = true;
        bool active = false;
        Vec3 right{};
        Vec3 direction{};
        Vec3 up{};
        Mat4 view{};

        void UpdateViewMatrix();
    };
}
