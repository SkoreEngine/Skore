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

#include "EnvironmentComponent.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/World/World.hpp"

namespace Skore
{
	void EnvironmentComponent::Create()
	{
		m_renderStorage = GetWorld()->GetRenderStorage();
		m_renderStorage->RegisterEnvironmentProxy(this);
		m_renderStorage->SetEnvironmentSkyboxMaterial(this, m_skyboxMaterial);
	}

	void EnvironmentComponent::Destroy()
	{
		if (m_renderStorage)
		{
			m_renderStorage->RemoveEnvironmentProxy(this);
		}
	}

	void EnvironmentComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_renderStorage) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_renderStorage->SetEnvironmentVisible(this, true);
				break;
			case EntityEventType::EntityDeactivated:
				m_renderStorage->SetEnvironmentVisible(this, false);
				break;
		};
	}

	TypedRID<MaterialResource> EnvironmentComponent::GetSkyboxMaterial() const
	{
		return m_skyboxMaterial;
	}

	void EnvironmentComponent::SetSkyboxMaterial(TypedRID<MaterialResource> skyboxMaterial)
	{
		m_skyboxMaterial = skyboxMaterial;
		if (m_renderStorage)
		{
			m_renderStorage->SetEnvironmentSkyboxMaterial(this, m_skyboxMaterial);
		}
	}

	void EnvironmentComponent::RegisterType(NativeReflectType<EnvironmentComponent>& type)
	{
		type.Field<&EnvironmentComponent::m_skyboxMaterial, &EnvironmentComponent::GetSkyboxMaterial, &EnvironmentComponent::SetSkyboxMaterial>("skyboxMaterial");
	}
}
