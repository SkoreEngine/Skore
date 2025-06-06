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

#include "Component.hpp"
#include "Entity.hpp"
#include "Scene.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void Component::EnableUpdate(bool enable)
	{
		bool wasEnabled = m_updateEnabled;
		m_updateEnabled = enable;

		if (enable && !wasEnabled)
		{
			m_scene->RegisterComponentForUpdate(this);
		}
		else if (!enable && wasEnabled)
		{
			m_scene->UnregisterComponentForUpdate(this);
		}
	}

	bool Component::IsUpdateEnabled() const
	{
		return m_updateEnabled;
	}

	//temporary
	bool Component::CanUpdate() const
	{
		return m_updateEnabled && m_entity->m_parentActivated && m_entity->m_active;
	}

	Scene* Component::GetScene() const
	{
		return m_scene;
	}

	UUID Component::GetUUID() const
	{
		return m_uuid;
	}

	UUID Component::GetPrefab() const
	{
		return m_prefab;
	}

	bool Component::IsPrefab() const
	{
		return m_prefab != UUID{};
	}

	Entity* Component::GetEntity() const
	{
		return m_entity;
	}

	void Component::Serialize(ArchiveWriter& archiveWriter) const
	{
		ReflectType* reflectType = GetType();
		for (ReflectField* field : reflectType->GetFields())
		{
			if (!m_prefab || m_overrides.Has(field->GetName()))
			{
				field->Serialize(archiveWriter, this);
			}
		}
	}

	void Component::Deserialize(ArchiveReader& archiveReader)
	{
		ReflectType* reflectType = GetType();
		while (archiveReader.NextMapEntry())
		{
			if (ReflectField* field = reflectType->FindField(archiveReader.GetCurrentKey()))
			{
				if (m_prefab)
				{
					m_overrides.Emplace(field->GetName());
				}
				field->Deserialize(archiveReader, this);
			}
		}
	}

	void Component::RegisterType(NativeReflectType<Component>& type)
	{
		type.Function<&Component::EnableUpdate>("EnableUpdate", "enable");
		type.Function<&Component::IsUpdateEnabled>("IsUpdateEnabled");
		type.Function<&Component::Start>("Start");
		type.Function<&Component::Update>("Update", "deltaTime");
	}
}
