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

#include "FreeViewCamera.hpp"

#include "Skore/IO/Input.hpp"

namespace Skore
{
	FreeViewCamera::FreeViewCamera()
	{
		UpdateViewMatrix();
	}

	void FreeViewCamera::Process(f64 deltaTime)
	{
		if (active)
		{
			if (firstMouse)
			{
				Input::SetCursorLockMode(CursorLockMode::Locked);
				previousMouseOffset = {};
				previousMovement = {};
				firstMouse = false;
			}

			// Handle mouse movement with smoothing
			Vec2 currentMouseOffset = Input::GetMouseAxis() * sensibility * deltaTime;
			
			// Apply smoothing to mouse input using exponential moving average
			Vec2 smoothedMouseOffset;
			smoothedMouseOffset.x = previousMouseOffset.x * smoothingFactor + currentMouseOffset.x * (1.0f - smoothingFactor);
			smoothedMouseOffset.y = previousMouseOffset.y * smoothingFactor + currentMouseOffset.y * (1.0f - smoothingFactor);
			
			// Store for next frame
			previousMouseOffset = smoothedMouseOffset;

			// Apply smoothed mouse movement
			yaw += smoothedMouseOffset.x;
			pitch += smoothedMouseOffset.y;

			// Clamp pitch to avoid gimbal lock
			pitch = Math::Clamp(pitch, -Math::Radians(89.0f), Math::Radians(89.0f));

			Quat pitchRotation = Math::AngleAxis(pitch, Vec3{1.0f, 0.0f, 0.0f});
			Quat yawRotation = Math::AngleAxis(yaw, Vec3{0.0f, 1.0f, 0.0f});
			rotation = Math::Normalize(pitchRotation * yawRotation);

			// Handle keyboard movement
			Vec3 targetMovement{};
			f32 localCameraSpeed = cameraSpeed;

			if (Input::IsKeyDown(Key::LeftShift))
			{
				localCameraSpeed *= 3.f;
			}

			if (Input::IsKeyDown(Key::A))
			{
				targetMovement += right * -localCameraSpeed;
			}
			if (Input::IsKeyDown(Key::D))
			{
				targetMovement += right * localCameraSpeed;
			}
			if (Input::IsKeyDown(Key::W))
			{
				targetMovement += direction * -localCameraSpeed;
			}
			if (Input::IsKeyDown(Key::S))
			{
				targetMovement += direction * localCameraSpeed;
			}
			if (Input::IsKeyDown(Key::E))
			{
				targetMovement += up * localCameraSpeed;
			}
			if (Input::IsKeyDown(Key::Q))
			{
				targetMovement += up * -localCameraSpeed;
			}

			// Apply smoothing to keyboard movement
			Vec3 smoothedMovement;
			smoothedMovement.x = previousMovement.x * movementSmoothingFactor + targetMovement.x * (1.0f - movementSmoothingFactor);
			smoothedMovement.y = previousMovement.y * movementSmoothingFactor + targetMovement.y * (1.0f - movementSmoothingFactor);
			smoothedMovement.z = previousMovement.z * movementSmoothingFactor + targetMovement.z * (1.0f - movementSmoothingFactor);
			
			// Store for next frame
			previousMovement = smoothedMovement;

			// Apply smoothed movement with delta time
			position += smoothedMovement * deltaTime;

			UpdateViewMatrix();
		}
		else if (!firstMouse)
		{
			firstMouse = true;
			Input::SetCursorLockMode(CursorLockMode::None);
		}
	}

	void FreeViewCamera::SetActive(bool p_active)
	{
		active = p_active;
	}

	void FreeViewCamera::UpdateViewMatrix()
	{
		view = Math::ToMatrix4(rotation) * Math::Translate(position * -1.f) * Math::Scale(scale);
		right = {view[0][0], view[1][0], view[2][0]};
		up = {view[0][1], view[1][1], view[2][1]};
		direction = {view[0][2], view[1][2], view[2][2]};
	}
}
