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
#include "Skore/Graphics/RenderStorage.hpp"
#include "Skore/Scene/Component.hpp"


namespace Skore
{
	class SK_API Camera : public Component
	{
	public:
		SK_CLASS(Camera, Component);

		void Create(ComponentSettings& settings) override;
		void Destroy() override;

		void ProcessEvent(const EntityEventDesc& event) override;

		enum class Projection : i32
		{
			Perspective = 1,
			Orthogonal  = 2
		};

		static void RegisterType(NativeReflectType<Camera>& type);

	private:
		RenderStorage* m_renderStorage = nullptr;


		Projection m_projection = Projection::Perspective;
		f32        m_fov = 60;
		f32        m_near = 0.1;
		f32        m_far = 1000.f;
		bool       m_current = false;
	};
}
