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

#include "MeshRenderer.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderStorage.hpp"
#include "Skore/World/Entity.hpp"
#include "Skore/World/World.hpp"


namespace Skore
{
	void MeshRenderer::Create()
	{
		m_renderStorage = GetWorld()->GetRenderStorage();

		m_renderStorage->RegisterMeshProxy(this);
		m_renderStorage->SetMeshTransform(this, entity->GetWorldTransform());
		m_renderStorage->SetMesh(this, m_mesh);
		m_renderStorage->SetMeshMaterials(this, CastRIDArray(m_materials));
		m_renderStorage->SetMeshCastShadows(this, m_castShadows);
	}

	void MeshRenderer::Destroy()
	{
		if (!m_renderStorage) return;
		m_renderStorage->RemoveMeshProxy(this);
	}

	void MeshRenderer::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_renderStorage) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_renderStorage->SetMeshVisible(this, true);
				break;
			case EntityEventType::EntityDeactivated:
				m_renderStorage->SetMeshVisible(this, false);
				break;
			case EntityEventType::TransformUpdated:
				m_renderStorage->SetMeshTransform(this, entity->GetWorldTransform());
				break;
		}
	}

	void MeshRenderer::SetMesh(RID mesh)
	{
		m_mesh = mesh;
		if (m_renderStorage)
		{
			m_renderStorage->SetMesh(this, m_mesh);
		}
	}

	RID MeshRenderer::GetMesh() const
	{
		return m_mesh;
	}

	void MeshRenderer::SetCastShadows(bool castShadows)
	{
		m_castShadows = castShadows;
		if (m_renderStorage)
		{
			m_renderStorage->SetMeshCastShadows(this, castShadows);
		}
	}

	bool MeshRenderer::GetCastShadows() const
	{
		return m_castShadows;
	}

	const MaterialArray& MeshRenderer::GetMaterials() const
	{
		return m_materials;
	}

	void MeshRenderer::SetMaterials(const MaterialArray& materials)
	{
		m_materials = materials;
		if (m_renderStorage)
		{
			m_renderStorage->SetMeshMaterials(this, CastRIDArray(materials));
		}
	}

	void MeshRenderer::RegisterType(NativeReflectType<MeshRenderer>& type)
	{
		type.Field<&MeshRenderer::m_mesh, &MeshRenderer::GetMesh, &MeshRenderer::SetMesh>("mesh");
		type.Field<&MeshRenderer::m_materials, &MeshRenderer::GetMaterials, &MeshRenderer::SetMaterials>("materials");
		type.Field<&MeshRenderer::m_castShadows, &MeshRenderer::GetCastShadows, &MeshRenderer::SetCastShadows>("castShadows");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
	}
}
