// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	class Component;

	class SK_API Entity final : public Object
	{
	public:
		SK_CLASS(Entity, Object);
		SK_NO_COPY_CONSTRUCTOR(Entity);

		~Entity() override = default;


		//general properties
		UUID       GetUUID() const;
		StringView GetName() const;
		void       SetName(StringView name);
		void       SetActive(bool active);
		bool       IsActive() const;


		//child-parent
		void          AddChild(Entity* child);
		Entity*       GetParent() const;
		bool          HasChildren() const;
		bool          IsChildOf(const Entity* parent) const;
		Span<Entity*> Children() const;

		//components
		Component* AddComponent(TypeID typeId);
		Span<Component*> GetAllComponents() const;


		template <typename T = Component, typename = std::enable_if_t<std::is_base_of_v<Component, T>>>
		T* AddComponent()
		{
			return static_cast<T*>(AddComponent(TypeInfo<T>::ID()));
		}

		//transform
		SK_FINLINE const Mat4& GetWorldTransform() const
		{
			return m_worldTransform;
		}

		SK_FINLINE void SetPosition(const Vec3& position)
		{
			m_transform.position = position;
			UpdateTransform();
		}

		SK_FINLINE void SetRotation(const Quat& rotation)
		{
			m_transform.rotation = rotation;
			UpdateTransform();
		}

		SK_FINLINE void SetScale(const Vec3& scale)
		{
			m_transform.scale = scale;
			UpdateTransform();
		}

		SK_FINLINE void SetTransform(const Vec3& position, const Quat& rotation, const Vec3& scale)
		{
			m_transform.position = position;
			m_transform.rotation = rotation;
			m_transform.scale = scale;
			UpdateTransform();
		}

		SK_FINLINE void SetTransform(const Transform& transform)
		{
			m_transform = transform;
			UpdateTransform();
		}

		SK_FINLINE const Transform& GetTransform() const
		{
			return m_transform;
		}

		SK_FINLINE const Vec3& GetPosition() const
		{
			return m_transform.position;
		}

		SK_FINLINE Vec3 GetWorldPosition() const
		{
			return Math::GetTranslation(m_worldTransform);
		}

		SK_FINLINE const Quat& GetRotation() const
		{
			return m_transform.rotation;
		}

		SK_FINLINE const Vec3& GetScale() const
		{
			return m_transform.scale;
		}

		SK_FINLINE Mat4 GetLocalTransform() const
		{
			return Math::Translate(Mat4{1.0}, m_transform.position) * Math::ToMatrix4(m_transform.rotation) * Math::Scale(Mat4{1.0}, m_transform.scale);
		}

		//static
		static Entity* Instantiate(UUID uuid, StringView name);
		static Entity* Instantiate();
		static Entity* FindByUUID(const UUID& uuid);
		static void    RegisterType(NativeReflectType<Entity>& type);

	private:
		Entity() = default;

		UUID      m_uuid;
		String    m_name;
		bool	  m_active = true;
		Transform m_transform = {};
		Mat4      m_worldTransform{1.0};

		Entity*        m_parent = nullptr;
		Array<Entity*> m_children;

		//components
		Array<Component*> m_components;

		void UpdateTransform();
	};
}
