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

#include "SkinnedMeshRenderer.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderStorage.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	void SkinnedMeshRenderer::Create(ComponentSettings& settings)
	{
		m_renderStorage = GetScene()->GetRenderStorage();

		m_renderStorage->RegisterSkinnedMeshProxy(this);
		m_renderStorage->SetSkinnedMeshTransform(this, entity->GetGlobalTransform());
		m_renderStorage->SetSkinnedMesh(this, m_mesh);
		m_renderStorage->SetSkinnedMeshMaterials(this, CastRIDArray(m_materials));
		m_renderStorage->SetSkinnedMeshCastShadows(this, m_castShadows);
	}

	void SkinnedMeshRenderer::Destroy()
	{
		m_renderStorage->RemoveSkinnedMeshProxy(this);
	}

	void SkinnedMeshRenderer::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_renderStorage) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_renderStorage->SetSkinnedMeshVisible(this, true);
				break;
			case EntityEventType::EntityDeactivated:
				m_renderStorage->SetSkinnedMeshVisible(this, false);
				break;
			case EntityEventType::TransformUpdated:
				m_renderStorage->SetSkinnedMeshTransform(this, entity->GetGlobalTransform());
				break;
		}
	}

	void SkinnedMeshRenderer::SetMesh(RID mesh)
	{
		m_mesh = mesh;
		if (m_renderStorage)
		{
			m_renderStorage->SetSkinnedMesh(this, m_mesh);
		}
	}

	RID SkinnedMeshRenderer::GetMesh() const
	{
		return m_mesh;
	}

	void SkinnedMeshRenderer::SetCastShadows(bool castShadows)
	{
		m_castShadows = castShadows;
		if (m_renderStorage)
		{
			m_renderStorage->SetSkinnedMeshCastShadows(this, castShadows);
		}
	}

	bool SkinnedMeshRenderer::GetCastShadows() const
	{
		return m_castShadows;
	}

	void SkinnedMeshRenderer::SetRootBone(Entity* rootBone)
	{
		m_rootBone = rootBone;
		// if (m_renderStorage)
		// {
		// 	m_renderStorage->SetSkinnedMeshRootBone(this, rootBone);
		// }
	}

	Entity* SkinnedMeshRenderer::GetRootBone() const
	{
		return m_rootBone;
	}

	const MaterialArray& SkinnedMeshRenderer::GetMaterials() const
	{
		return m_materials;
	}

	void SkinnedMeshRenderer::SetMaterials(const MaterialArray& materials)
	{
		m_materials = materials;
		if (m_renderStorage)
		{
			m_renderStorage->SetSkinnedMeshMaterials(this, CastRIDArray(materials));
		}
	}

	void SkinnedMeshRenderer::RegisterType(NativeReflectType<SkinnedMeshRenderer>& type)
	{
		type.Field<&SkinnedMeshRenderer::m_mesh, &SkinnedMeshRenderer::GetMesh, &SkinnedMeshRenderer::SetMesh>("mesh");
		type.Field<&SkinnedMeshRenderer::m_rootBone, &SkinnedMeshRenderer::GetRootBone, &SkinnedMeshRenderer::SetRootBone>("rootBone");
		type.Field<&SkinnedMeshRenderer::m_materials, &SkinnedMeshRenderer::GetMaterials, &SkinnedMeshRenderer::SetMaterials>("materials");
		type.Field<&SkinnedMeshRenderer::m_castShadows, &SkinnedMeshRenderer::GetCastShadows, &SkinnedMeshRenderer::SetCastShadows>("castShadows");
	}
}
