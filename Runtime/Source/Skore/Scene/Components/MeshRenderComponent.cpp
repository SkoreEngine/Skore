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

#include "MeshRenderComponent.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"
#include "Skore/Graphics/RenderStorage.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneTypes.hpp"

namespace Skore
{
	void MeshRenderComponent::Init()
	{
		m_renderStorage = GetScene()->GetRenderStorage();
		m_renderStorage->RegisterMeshProxy(this);
		m_renderStorage->SetMeshTransform(this, GetEntity()->GetWorldTransform());
	}

	void MeshRenderComponent::Destroy()
	{
		m_renderStorage->RemoveMeshProxy(this);
	}

	void MeshRenderComponent::ProcessEvent(const SceneEventDesc& event)
	{
		switch (event.type)
		{
			case SceneEventType::EntityActivated:
				m_renderStorage->SetMeshVisible(this, true);
				break;
			case SceneEventType::EntityDeactivated:
				m_renderStorage->SetMeshVisible(this, false);
				break;
			case SceneEventType::TransformUpdated:
				m_renderStorage->SetMeshTransform(this, GetEntity()->GetWorldTransform());
				break;
		}
	}

	void MeshRenderComponent::SetMesh(MeshAsset* mesh)
	{
		m_mesh = mesh;
		m_renderStorage->SetMesh(this, m_mesh);
	}

	MeshAsset* MeshRenderComponent::GetMesh() const
	{
		return m_mesh;
	}

	const Array<MaterialAsset*>& MeshRenderComponent::GetMaterials() const
	{
		return m_materials;
	}

	void MeshRenderComponent::SetMaterials(const Array<MaterialAsset*>& materials)
	{
		m_materials = materials;
		m_renderStorage->SetMeshMaterials(this, materials);
	}

	void MeshRenderComponent::SetCastShadows(bool castShadows)
	{
		m_castShadows = castShadows;
		m_renderStorage->SetMeshCastShadows(this, castShadows);
	}

	bool MeshRenderComponent::GetCastShadows() const
	{
		return m_castShadows;
	}

	void MeshRenderComponent::RegisterType(NativeReflectType<MeshRenderComponent>& type)
	{
		type.Field<&MeshRenderComponent::m_mesh, &MeshRenderComponent::GetMesh, &MeshRenderComponent::SetMesh>("mesh");
		type.Field<&MeshRenderComponent::m_materials, &MeshRenderComponent::GetMaterials, &MeshRenderComponent::SetMaterials>("materials");
		type.Field<&MeshRenderComponent::m_castShadows, &MeshRenderComponent::GetCastShadows, &MeshRenderComponent::SetCastShadows>("castShadows");
		type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = true});
	}
}
