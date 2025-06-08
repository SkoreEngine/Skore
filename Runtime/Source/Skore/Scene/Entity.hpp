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
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	struct SceneEventDesc;
	class Scene;
	class Component2;

	class SK_API Entity : public Object
	{
	public:
		Entity() = default;

		SK_CLASS(Entity, Object);
		SK_NO_COPY_CONSTRUCTOR(Entity);

		~Entity() override;

		void SetActive(bool active);
		bool IsActive() const;
		bool HasChildren() const;

		StringView    GetName() const;
		void          SetName(StringView name);
		UUID          GetUUID() const;
		void          SetUUID(const UUID& uuid);
		Entity*       GetParent() const;
		void          SetParent(Entity* parent);
		void          DetachFromParent();
		u64           GetSiblingIndex() const;
		void          SetSiblingIndex(u64 index);
		bool          IsChildOf(Entity* parent) const;
		Span<Entity*> Children() const;
		Entity*		  FindChildByOrigin(UUID origin) const;
		Entity*       Duplicate() const;
		Entity*       Duplicate(Entity* newParent) const;
		void          Destroy() const;

		void SetOverride(StringView field);
		void RemoveOverride(StringView field);
		bool HasOverride(StringView field) const;
		bool HasOverrides() const;
		void ClearOverrides();

		Component2* AddComponent(TypeID typeId);
		Component2* AddComponent(ReflectType* reflectType);
		Component2* AddComponent(ReflectType* reflectType, UUID uuid);
		Component2* FindComponentByUUID(UUID uuid);
		Component2* FindComponentByPrefab(UUID uuid);


		void RemoveComponent(Component2* component);
		void RemoveComponent(TypeID typeId);
		void RemoveComponentAt(u32 index);

		void MoveComponentTo(Component2* component, u32 index);

		u32               GetComponentIndex(Component2* component);
		Component2*        GetComponent(TypeID typeId) const;
		Array<Component2*> GetComponents(TypeID typeId) const;

		UUID GetPrefab() const;

		Scene* GetScene() const;

		virtual void NotifyEvent(const SceneEventDesc& event, bool notifyChildren = false);

		template <typename T>
		Array<T*> GetComponents() const
		{
			Array<T*> ret;

			auto arr = GetComponents(TypeInfo<T>::ID());
			ret.Reserve(arr.Size());

			for (Component2* comp : arr)
			{
				ret.EmplaceBack(static_cast<T*>(comp));
			}
			return ret;
		}

		template <typename T>
		T* GetComponent() const
		{
			return static_cast<T*>(GetComponent(TypeInfo<T>::ID()));
		}

		Span<Component2*> GetAllComponents() const;

		template <typename T>
		void RemoveComponent(T* component)
		{
			RemoveComponent(static_cast<Component2*>(component));
		}

		template <typename T>
		T* AddComponent()
		{
			return static_cast<T*>(AddComponent(TypeInfo<T>::ID()));
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

		SK_FINLINE const Mat4& GetWorldTransform() const
		{
			return m_worldTransform;
		}

		SK_FINLINE Mat4 GetLocalTransform() const
		{
			return Math::Translate(Mat4{1.0}, m_transform.position) * Math::ToMatrix4(m_transform.rotation) * Math::Scale(Mat4{1.0}, m_transform.scale);
		}

		static Entity* Instantiate(Entity* parent, StringView name = "Entity", UUID uuid = {});
		static Entity* Instantiate(UUID prefab, Entity* parent, StringView name = "Entity", UUID uuid = {});

		void Serialize(ArchiveWriter& archiveWriter) const override;
		void Deserialize(ArchiveReader& archiveReader) override;

		void SerializeWithChildren(ArchiveWriter& archiveWriter);
		void DeserializeWithChildren(ArchiveReader& archiveReader);

		friend class Scene;
		friend class Component2;

		static void RegisterType(NativeReflectType<Entity>& type);

	private:
		String            m_name;
		UUID              m_uuid;
		bool              m_active = true;
		bool              m_parentActivated = true;
		Entity*           m_parent = nullptr;
		Array<Component2*> m_components;
		Scene*            m_scene = nullptr;
		bool              m_started = false;
		Array<Entity*>    m_children;

		//prefabs
		UUID            m_prefab;
		UUID            m_origin;
		HashSet<String> m_overrides;
		HashSet<UUID>   m_removedEntities;
		HashSet<UUID>   m_removedComponents;

		//transform
		Transform m_transform = {};
		Mat4      m_worldTransform{1.0};
		void UpdateTransform();


		//components
		void       AddComponent(Component2* component);
		void       AddComponent(Component2* component, UUID uuid);


		void DoStart();
		void DestroyInternal(bool removeFromParent);

		void SetScene(Scene* scene);
		void SetParentActivated(bool parentActivated);

		void RemoveComponentAt(u32 index, bool destroy);

		static Entity* InstantiateFromOrigin(Entity* origin, Entity* parent);
		void LoadPrefab();
	};
}
