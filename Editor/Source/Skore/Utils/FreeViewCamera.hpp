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
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"


namespace Skore
{
	class FreeViewCamera
	{
	public:
		FreeViewCamera();
		void Process(f64 deltaTime);

		void SetActive(bool p_active);

		bool IsActive() const
		{
			return active;
		}

		const Mat4& GetView() const
		{
			return view;
		}

		Vec3 GetPosition() const
		{
			return position;
		}

		Quat GetRotation() const
		{
			return rotation;
		}

		Vec3 GetScale() const
		{
			return scale;
		}

		void SetSensibility(float p_sensibility)
		{
			sensibility = p_sensibility;
		}

		// Mouse smoothing parameters
		f32 smoothingFactor = 0.7f; // Higher value = more smoothing (0.0-0.95)
		f32 movementSmoothingFactor = 0.85f;
		f32 cameraSpeed = 10.0f;

	private:
		Vec3 position{0, 0, 0};
		Quat rotation{0, 0, 0, 1};
		Vec3 scale{1, 1, 1};


		f32  yaw{};
		f32  pitch{};
		bool firstMouse = true;
		bool active = false;
		Vec3 right{};
		Vec3 direction{};
		Vec3 up{};
		Mat4 view = Mat4(1.0);
		float sensibility = 1.0f;
		
		Vec2 previousMouseOffset{};
		Vec3 previousMovement{};

		void UpdateViewMatrix();
	};
}
