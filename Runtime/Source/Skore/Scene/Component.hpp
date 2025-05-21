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
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	struct SceneEventDesc;
	class Scene;
	class Entity;

	class SK_API Component : public Object
	{
	public:
		SK_CLASS(Component, Object);
		SK_NO_COPY_CONSTRUCTOR(Component);

		Component() = default;

		virtual void Init() {}
		virtual void Destroy() {}
		virtual void Start() {}
		virtual void Update(f64 deltaTime) {}

		virtual void ProcessEvent(const SceneEventDesc& event) {}

		void EnableUpdate(bool enable);
		bool IsUpdateEnabled() const;

		bool   CanUpdate() const;
		Scene* GetScene() const;
		UUID   GetUUID() const;
		UUID   GetPrefab() const;
		bool   IsPrefab() const;

		Entity* GetEntity() const;

		void Serialize(ArchiveWriter& archiveWriter) const override;
		void Deserialize(ArchiveReader& archiveReader) override;

		static void RegisterType(NativeReflectType<Component>& type);

		friend class Entity;
	private:
		UUID            m_uuid;
		UUID            m_prefab;
		bool            m_updateEnabled = false;
		Scene*          m_scene = nullptr;
		Entity*         m_entity = nullptr;
		HashSet<String> m_overrides;
	};
}
