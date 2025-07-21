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
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Graphics/RenderStorage.hpp"
#include "Physics.hpp"
#include "Skore/Resource/ResourceReflection.hpp"

namespace Skore
{
	class Component;
	class Entity;

	class SK_API Scene : public Object
	{
	public:
		SK_CLASS(Scene, Object);
		SK_NO_COPY_CONSTRUCTOR(Scene);

		Scene() = default;
		Scene(RID rid, bool enableResourceSync = false);
		~Scene() override;

		Entity* GetRootEntity() const;
		bool    IsResourceSyncEnabled() const;

		RenderStorage* GetRenderStorage();
		PhysicsScene*  GetPhysicsScene();

		Entity* FindEntityByRID(RID rid) const;

		friend class Entity;
		friend class SceneManager;
		friend class Component;
		friend class SceneEditor;

		friend class ResourceCast<Entity*>;
	private:
		Entity* m_rootEntity = nullptr;
		bool m_enableResourceSync = false;

		RenderStorage m_renderStorage;
		PhysicsScene m_physicsScene;

		Queue<Entity*>    m_queueToStart;
		Queue<Component*> m_componentsToStart;
		Queue<Entity*>    m_queueToDestroy;

		ankerl::unordered_dense::set<Component*> m_updateComponents = {};
		ankerl::unordered_dense::set<Component*> m_fixedUpdateComponents = {};

		HashMap<RID, Entity*> m_entities;

		Entity* FindOrCreateInstance(RID rid);

		void OnSceneDeactivated();
		void OnSceneActivated();
		void ExecuteEvents();
		void Update();
		void DoReflectionUpdated();
	};
}
