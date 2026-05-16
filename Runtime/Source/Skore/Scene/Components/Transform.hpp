#pragma once

#include "Skore/Core/Math.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	enum class EntityMobility
	{
		Static,
		Mixed,
		Dynamic,
	};


	class SK_API Transform : public Component
	{
	public:
		SK_CLASS(Transform, Component)

		enum
		{
			UpdateTransform_Position = 1 << 0,
			UpdateTransform_Rotation = 1 << 1,
			UpdateTransform_Scale    = 1 << 2,
			UpdateTransform_All      = UpdateTransform_Position | UpdateTransform_Rotation | UpdateTransform_Scale,
		};

		enum
		{
			Position,
			Rotation,
			Scale
		};

		SK_FINLINE void SetPosition(const Vec3& position)
		{
			m_position = position;
			UpdateTransform(UpdateTransform_Position);
		}

		SK_FINLINE void SetRotation(const Quat& rotation)
		{
			m_rotation = rotation;
			UpdateTransform(UpdateTransform_Rotation);
		}

		SK_FINLINE void SetRotationEuler(const Vec3& rotation)
		{
			m_rotation = Quat(Vec3::Radians(rotation));
			UpdateTransform(UpdateTransform_Rotation);
		}

		SK_FINLINE void SetScale(const Vec3& scale)
		{
			m_scale = scale;
			UpdateTransform(UpdateTransform_Scale);
		}

		SK_FINLINE void SetTransform(const Vec3& position, const Quat& rotation, const Vec3& scale)
		{

			u32 flags = 0;

			if (m_position != position)
			{
				m_position = position;
				flags |= UpdateTransform_Position;
			}

			if (m_scale != scale)
			{
				m_scale = scale;
				flags |= UpdateTransform_Scale;
			}

			if (m_rotation != rotation)
			{
				m_rotation = rotation;
				flags |= UpdateTransform_Rotation;
			}

			if (flags != 0)
			{
				UpdateTransform(flags);
			}

		}

		SK_FINLINE const Vec3& GetPosition() const
		{
			return m_position;
		}

		SK_FINLINE const Quat& GetRotation() const
		{
			return m_rotation;
		}

		SK_FINLINE const Vec3& GetScale() const
		{
			return m_scale;
		}

		SK_FINLINE Mat4 GetLocalTransform() const
		{
			return Mat4::Translate(Mat4{1.0}, m_position) * Quat::ToMatrix4(m_rotation) * Mat4::Scale(Mat4{1.0}, m_scale);
		}

		EntityMobility GetMobility() const
		{
			return m_mobility;
		}

		void SetMobility(EntityMobility mobility)
		{
			m_mobility = mobility;
		}


		const Mat4& GetParentWorldTransform() const;

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<Transform>& type);

	private:
		Vec3 m_position{0, 0, 0};
		Quat m_rotation{0, 0, 0, 1};
		Vec3 m_scale{1, 1, 1};
		EntityMobility m_mobility = EntityMobility::Static;

		void UpdateTransform(u32 flags);
	};

	class Transform2D : public Component
	{
	public:
		SK_CLASS(Transform2D, Component)

		enum
		{
			Position,
			Scale,
			Rotation,
		};

		static void RegisterType(NativeReflectType<Transform2D>& type);

	private:
		Vec2  m_position{0, 0};
		Vec2  m_scale{1, 1};
		Float m_rotation = 0.0f;
	};
}
