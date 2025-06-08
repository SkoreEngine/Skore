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
#include "Entity.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/IO/Assets.hpp"

namespace Skore
{
	class RenderStorage;

	class SK_API Scene final : public Asset
	{
	public:
		SK_CLASS(Scene, Asset);
		SK_NO_COPY_CONSTRUCTOR(Scene);

		Scene();
		~Scene() override;

		void FlushQueues();
		void Update(f64 deltaTime);

		void    SetRootEntity(Entity* rootEntity);
		Entity* GetRootEntity() const;

		void Serialize(ArchiveWriter& archiveWriter) const override;
		void Deserialize(ArchiveReader& archiveReader) override;

		Entity* FindEntityByUUID(UUID uuid);

		RenderStorage* GetRenderStorage() const;

		friend class Component2;
		friend class Entity;

		static void RegisterType(NativeReflectType<Scene>& type);

	private:
		void RegisterComponentForUpdate(Component2* component);
		void UnregisterComponentForUpdate(Component2* component);

		Entity* m_rootEntity = nullptr;

		ankerl::unordered_dense::set<Component2*> m_updateComponents = {};
		HashMap<UUID, Entity*>                   m_entities;

		Queue<Entity*>    m_queueToStart;
		Queue<Component2*> m_componentsToStart;
		Queue<Entity*>    m_queueToDestroy;

		RenderStorage* m_renderStorage = nullptr;
	};
}
