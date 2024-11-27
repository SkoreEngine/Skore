#pragma once

#include "Component.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
    class SK_API TransformComponent : public Component
    {
    public:
        SK_BASE_TYPES(Component);

        SK_FINLINE void SetPosition(const Vec3& position)
        {
            this->position = position;
            UpdateTransform();
        }

        SK_FINLINE void SetRotation(const Quat& rotation)
        {
            this->rotation = rotation;
            UpdateTransform();
        }

        SK_FINLINE void SetScale(const Vec3& scale)
        {
            this->scale = scale;
            UpdateTransform();
        }

        SK_FINLINE void SetTransform(const Vec3& position, const Quat& rotation, const Vec3& scale)
        {
            this->position = position;
            this->rotation = rotation;
            this->scale = scale;
            UpdateTransform();
        }

        SK_FINLINE void SetTransform(const Transform& transform)
        {
            SetTransform(transform.position, transform.rotation, transform.scale);
        }

        SK_FINLINE const Vec3& GetPosition() const
        {
            return position;
        }

        SK_FINLINE Vec3 GetWorldPosition() const
        {
            return Math::GetTranslation(worldTransform);
        }

        SK_FINLINE const Quat& GetRotation() const
        {
            return rotation;
        }

        SK_FINLINE const Vec3& GetScale() const
        {
            return scale;
        }

        SK_FINLINE const Mat4& GetWorldTransform() const
        {
            return worldTransform;
        }

        SK_FINLINE Mat4 GetLocalTransform() const
        {
            return Math::Translate(Mat4{1.0}, position) * Math::ToMatrix4(rotation) * Math::Scale(Mat4{1.0}, scale);
        }

        SK_FINLINE Transform GetTransform() const
        {
            return {position, rotation, scale};
        }

        void OnStart() override;
        void OnChange() override;

        static void RegisterType(NativeTypeHandler<TransformComponent>& type);

    private:
        Vec3 position{0, 0, 0};
        Quat rotation{0, 0, 0, 1};
        Vec3 scale{1, 1, 1};
        Mat4 worldTransform{1.0};

        void UpdateTransform();
        void UpdateTransform(const TransformComponent* parentTransform);
    };
}
