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

#include "StaticMeshRenderer.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderStorage.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
{
	void StaticMeshRenderer::Create(ComponentSettings& settings)
	{
		m_renderStorage = GetScene()->GetRenderStorage();

		m_renderStorage->RegisterStaticMeshProxy(this, entity->GetRID().id); //TODO -> change to another thing.
		m_renderStorage->SetStaticMeshTransform(this, entity->GetGlobalTransform());
		m_renderStorage->SetStaticMesh(this, m_mesh);
		m_renderStorage->SetStaticMeshMaterials(this, CastRIDArray(m_materials));
		m_renderStorage->SetStaticMeshCastShadows(this, m_castShadows);
	}

	void StaticMeshRenderer::Destroy()
	{
		if (!m_renderStorage) return;
		m_renderStorage->RemoveStaticMeshProxy(this);
	}

	void StaticMeshRenderer::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_renderStorage) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_renderStorage->SetStaticMeshVisible(this, true);
				break;
			case EntityEventType::EntityDeactivated:
				m_renderStorage->SetStaticMeshVisible(this, false);
				break;
			case EntityEventType::TransformUpdated:
				m_renderStorage->SetStaticMeshTransform(this, entity->GetGlobalTransform());
				break;
		}
	}

	void StaticMeshRenderer::SetMesh(RID mesh)
	{
		m_mesh = mesh;
		if (m_renderStorage)
		{
			m_renderStorage->SetStaticMesh(this, m_mesh);
		}
	}

	RID StaticMeshRenderer::GetMesh() const
	{
		return m_mesh;
	}

	void StaticMeshRenderer::SetCastShadows(bool castShadows)
	{
		m_castShadows = castShadows;
		if (m_renderStorage)
		{
			m_renderStorage->SetStaticMeshCastShadows(this, castShadows);
		}
	}

	bool StaticMeshRenderer::GetCastShadows() const
	{
		return m_castShadows;
	}

	const MaterialArray& StaticMeshRenderer::GetMaterials() const
	{
		return m_materials;
	}

	void StaticMeshRenderer::SetMaterials(const MaterialArray& materials)
	{
		m_materials = materials;
		if (m_renderStorage)
		{
			m_renderStorage->SetStaticMeshMaterials(this, CastRIDArray(materials));
		}
	}

	void StaticMeshRenderer::RegisterType(NativeReflectType<StaticMeshRenderer>& type)
	{
		type.Field<&StaticMeshRenderer::m_mesh, &StaticMeshRenderer::GetMesh, &StaticMeshRenderer::SetMesh>("mesh");
		type.Field<&StaticMeshRenderer::m_materials, &StaticMeshRenderer::GetMaterials, &StaticMeshRenderer::SetMaterials>("materials");
		type.Field<&StaticMeshRenderer::m_castShadows, &StaticMeshRenderer::GetCastShadows, &StaticMeshRenderer::SetCastShadows>("castShadows");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
	}
}
