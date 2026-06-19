#pragma once
#include "Skore/Core/Math.hpp"
#include "Skore/Scene/Component.hpp"


namespace Skore
{
	class SK_API BoxCollider : public Component
	{
	public:
		SK_CLASS(BoxCollider, Component);

		void OnCreate() override;

		const Vec3& GetSize() const;
		void        SetSize(const Vec3& halfSize);
		const Vec3& GetCenter() const;
		void        SetCenter(const Vec3& center);
		f32         GetDensity() const;
		void        SetDensity(float density);
		bool        IsSensor() const;
		void        SetIsSensor(bool isSensor);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<BoxCollider>& type);

	private:
		bool m_isSensor = false;
		f32  m_density = 1000.0f;
		Vec3 m_size = {1.0, 1.0, 1.0};
		Vec3 m_center = {0.0, 0.0, 0.0};
	};

	class SK_API SphereCollider : public Component
	{
	public:
		SK_CLASS(SphereCollider, Component);

		void OnCreate() override;

		f32         GetRadius() const;
		void        SetRadius(f32 radius);
		const Vec3& GetCenter() const;
		void        SetCenter(const Vec3& center);
		f32         GetDensity() const;
		void        SetDensity(f32 density);
		bool        IsSensor() const;
		void        SetIsSensor(bool isSensor);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<SphereCollider>& type);

	private:
		bool m_isSensor = false;
		f32  m_density = 1000.0f;
		f32  m_radius = 0.5f;
		Vec3 m_center = {0.0, 0.0, 0.0};
	};

	class SK_API CapsuleCollider : public Component
	{
	public:
		SK_CLASS(CapsuleCollider, Component);

		void OnCreate() override;

		f32         GetRadius() const;
		void        SetRadius(f32 radius);
		f32         GetHeight() const;
		void        SetHeight(f32 height);
		const Vec3& GetCenter() const;
		void        SetCenter(const Vec3& center);
		f32         GetDensity() const;
		void        SetDensity(f32 density);
		bool        IsSensor() const;
		void        SetIsSensor(bool isSensor);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<CapsuleCollider>& type);

	private:
		bool m_isSensor = false;
		f32  m_density = 1000.0f;
		f32  m_radius = 0.5f;
		f32  m_height = 1.0f;
		Vec3 m_center = {0.0, 0.0, 0.0};
	};

	class SK_API CylinderCollider : public Component
	{
	public:
		SK_CLASS(CylinderCollider, Component);

		void OnCreate() override;

		f32         GetRadius() const;
		void        SetRadius(f32 radius);
		f32         GetHeight() const;
		void        SetHeight(f32 height);
		const Vec3& GetCenter() const;
		void        SetCenter(const Vec3& center);
		f32         GetDensity() const;
		void        SetDensity(f32 density);
		bool        IsSensor() const;
		void        SetIsSensor(bool isSensor);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<CylinderCollider>& type);

	private:
		bool m_isSensor = false;
		f32  m_density = 1000.0f;
		f32  m_radius = 0.5f;
		f32  m_height = 1.0f;
		Vec3 m_center = {0.0, 0.0, 0.0};
	};
}
