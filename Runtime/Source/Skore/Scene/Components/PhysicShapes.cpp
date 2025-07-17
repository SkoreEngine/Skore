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

#include "PhysicShapes.hpp"

#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	const Vec3& BoxCollider::GetHalfSize() const
	{
		return m_halfSize;
	}

	void BoxCollider::SetHalfSize(const Vec3& halfSize)
	{
		this->m_halfSize = halfSize;
	}

	f32 BoxCollider::GetDensity() const
	{
		return m_density;
	}

	void BoxCollider::SetDensity(f32 density)
	{
		this->m_density = density;
	}

	bool BoxCollider::IsIsSensor() const
	{
		return m_isSensor;
	}

	void BoxCollider::SetIsSensor(bool isSensor)
	{
		this->m_isSensor = isSensor;
	}

	// void BoxColliderComponent::CollectShapes(Array<BodyShapeBuilder>& shapes)
	// {
	// 	shapes.EmplaceBack(BodyShapeBuilder{
	// 		.bodyShape = BodyShapeType::Box,
	// 		.size = halfSize,
	// 		.density = density,
	// 		.sensor = isSensor
	// 	});
	// }

	void BoxCollider::RegisterType(NativeReflectType<BoxCollider>& type)
	{
		type.Field<&BoxCollider::m_halfSize>("halfSize");
		type.Field<&BoxCollider::m_density>("density");
		type.Field<&BoxCollider::m_isSensor>("isSensor");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}
}
