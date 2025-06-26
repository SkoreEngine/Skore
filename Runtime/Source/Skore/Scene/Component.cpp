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


namespace Skore
{
	Scene* Component::GetScene() const
	{
		if (entity)
		{
			return entity->GetScene();
		}

		return nullptr;
	}

	void Component::RegisterEvents()
	{
		if (m_settings.enableUpdate)
		{
			entity->m_scene->m_updateComponents.emplace(this);
		}

		if (m_settings.enableFixedUpdate)
		{
			entity->m_scene->m_fixedUpdateComponents.emplace(this);
		}
	}

	void Component::RemoveEvents()
	{
		if (m_settings.enableUpdate)
		{
			entity->m_scene->m_updateComponents.erase(this);
		}

		if (m_settings.enableFixedUpdate)
		{
			entity->m_scene->m_fixedUpdateComponents.erase(this);
		}
	}
}
