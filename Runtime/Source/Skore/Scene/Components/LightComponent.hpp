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
#include "Skore/Core/Color.hpp"
#include "Skore/Scene/Component2.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class RenderStorage;

	class SK_API LightComponent : public Component2
	{
	public:
		SK_CLASS(LightComponent, Component2);

		enum class LightType
		{
			Directional,
			Point,
			Spot
		};


		void Init() override;
		void Destroy() override;
		void ProcessEvent(const SceneEventDesc& event) override;

		void      SetLightType(LightType type);
		LightType GetLightType() const;

		void         SetColor(const Color& color);
		const Color& GetColor() const;

		void SetIntensity(f32 intensity);
		f32  GetIntensity() const;

		void SetRange(f32 range);
		f32  GetRange() const;

		// Spot light specific properties
		void SetInnerConeAngle(f32 angle);
		f32  GetInnerConeAngle() const;

		void SetOuterConeAngle(f32 angle);
		f32  GetOuterConeAngle() const;

		void SetEnableShadows(bool enable);
		bool GetEnableShadows() const;

		static void RegisterType(NativeReflectType<LightComponent>& type);

	private:
		RenderStorage* m_renderStorage = nullptr;
		LightType      m_lightType = LightType::Directional;
		Color          m_color = Color::WHITE;
		f32            m_intensity = 1.0f;
		f32            m_range = 10.0f;
		f32            m_innerConeAngle = 25.0f;
		f32            m_outerConeAngle = 30.0f;
		bool           m_enableShadows = true;
	};
}
