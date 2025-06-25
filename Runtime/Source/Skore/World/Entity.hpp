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
#include "WorldCommon.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Object.hpp"

namespace Skore
{
	class Component;
	class World;

	class SK_API Entity final : public Object
	{
	public:
		SK_CLASS(Entity, Object);

		~Entity() override;

		Transform&       GetTransform();
		World*           GetWorld() const;
		void			 SetParent(Entity* newParent);
		Entity*          GetParent() const;
		Span<Entity*>    GetChildren() const;
		Span<Component*> GetComponents() const;
		RID              GetRID() const;
		RID				 GetTransformRID() const;

		void       SetName(StringView name);
		StringView GetName() const;

		bool IsActive() const;
		void SetActive(bool active);

		Entity* CreateChild();
		Entity* CreateChildFromAsset(RID rid);

		Component* AddComponent(TypeID typeId);
		Component* AddComponent(ReflectType* reflectType);
		Component* AddComponent(ReflectType* reflectType, RID rid);
		void RemoveComponent(Component* component);

		void NotifyEvent(const EntityEventDesc& event, bool notifyChildren = false);

		void Destroy();

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

		SK_FINLINE const Mat4& GetWorldTransform() const
		{
			return m_worldTransform;
		}

		SK_FINLINE Mat4 GetLocalTransform() const
		{
			return Math::Translate(Mat4{1.0}, m_transform.position) * Math::ToMatrix4(m_transform.rotation) * Math::Scale(Mat4{1.0}, m_transform.scale);
		}

		template <typename... Args>
		static Entity* Instantiate(Args&&... args)
		{
			Entity* entity = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
			new(PlaceHolder{}, entity) Entity(Traits::Forward<Args>(args)...);
			return entity;
		}


		friend class World;
	private:
		Entity(World* world);
		Entity(World* world, RID rid);
		Entity(World* world, Entity* parent);
		Entity(World* world, Entity* parent, RID rid);

		String m_name = {};
		RID m_rid = {};

		bool m_active = true;
		bool m_parentActive = false;

		World*         m_world = nullptr;
		Entity*        m_parent = nullptr;
		Array<Entity*> m_children;

		Array<Component*> m_components;

		Mat4      m_worldTransform{1.0};
		Transform m_transform;
		RID       m_transformRID;

		void DestroyInternal(bool removeFromParent = true);
		void UpdateTransform();

		void DestroyComponent(Component* component) const;

		static void OnEntityResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
		static void OnComponentResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
		static void OnTransformResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
	};
}
