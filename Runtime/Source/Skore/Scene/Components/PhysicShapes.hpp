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
#include "Skore/Core/Math.hpp"
#include "Skore/Scene/Component.hpp"


namespace Skore
{
	class SK_API BoxCollider : public Component
	{
	public:
		SK_CLASS(BoxCollider, Component);

		void Create(ComponentSettings& settings) override;

		const Vec3& GetSize() const;
		void        SetSize(const Vec3& halfSize);
		const Vec3& GetCenter() const;
		void        SetCenter(const Vec3& center);
		f32         GetDensity() const;
		void        SetDensity(float density);
		bool        IsSensor() const;
		void        SetIsSensor(bool isSensor);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<BoxCollider>& type);

	private:
		bool m_isSensor = false;
		f32  m_density = 1000.0f;
		Vec3 m_size = {1.0, 1.0, 1.0};
		Vec3 m_center = {0.0, 0.0, 0.0};
	};
}
