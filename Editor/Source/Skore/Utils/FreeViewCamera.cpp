#include "Skore/Utils/FreeViewCamera.hpp"

#include <cmath>

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
			f32 dt = static_cast<f32>(deltaTime);

			if (firstMouse)
			{
				Input::SetCursorLockMode(CursorLockMode::Locked);
				previousMouseOffset = {};
				previousMovement = {};
				firstMouse = false;
			}

			// Handle mouse movement with smoothing
			Vec2 currentMouseOffset = Input::GetMouseAxis() * sensitivity;

			// Frame-rate independent smoothing (normalized to 60fps reference rate)
			f32 mouseAlpha = 1.0f - std::pow(smoothingFactor, dt * 60.0f);
			Vec2 smoothedMouseOffset;
			smoothedMouseOffset.x = previousMouseOffset.x + (currentMouseOffset.x - previousMouseOffset.x) * mouseAlpha;
			smoothedMouseOffset.y = previousMouseOffset.y + (currentMouseOffset.y - previousMouseOffset.y) * mouseAlpha;

			// Store for next frame
			previousMouseOffset = smoothedMouseOffset;

			// Apply smoothed mouse movement
			yaw += smoothedMouseOffset.x;
			pitch += smoothedMouseOffset.y;

			// Clamp pitch to avoid gimbal lock
			pitch = Math::Clamp(pitch, -Math::Radians(89.0f), Math::Radians(89.0f));

			Quat pitchRotation = Quat::AngleAxis(pitch, Vec3{1.0f, 0.0f, 0.0f});
			Quat yawRotation = Quat::AngleAxis(yaw, Vec3{0.0f, 1.0f, 0.0f});
			rotation = Quat::Normalized(pitchRotation * yawRotation);

			// Adjust camera speed with mouse wheel
			f32 wheel = Input::GetMouseWheel().y;
			if (wheel != 0.0f)
			{
				cameraSpeed *= (wheel > 0.0f) ? 1.1f : 0.9f;
				cameraSpeed = Math::Clamp(cameraSpeed, 0.1f, 500.0f);
			}

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

			// Frame-rate independent smoothing for keyboard movement
			f32 moveAlpha = 1.0f - std::pow(movementSmoothingFactor, dt * 60.0f);
			Vec3 smoothedMovement;
			smoothedMovement.x = previousMovement.x + (targetMovement.x - previousMovement.x) * moveAlpha;
			smoothedMovement.y = previousMovement.y + (targetMovement.y - previousMovement.y) * moveAlpha;
			smoothedMovement.z = previousMovement.z + (targetMovement.z - previousMovement.z) * moveAlpha;

			// Store for next frame
			previousMovement = smoothedMovement;

			// Apply smoothed movement with delta time
			position += smoothedMovement * deltaTime;

			UpdateViewMatrix();
		}
		else if (!firstMouse)
		{
			firstMouse = true;
			previousMouseOffset = {};
			previousMovement = {};
			Input::SetCursorLockMode(CursorLockMode::Free);
		}
	}

	void FreeViewCamera::SetActive(bool p_active)
	{
		active = p_active;
	}

	void FreeViewCamera::SetPosition(Vec3 p_position)
	{
		position = p_position;
		UpdateViewMatrix();
	}

	void FreeViewCamera::SetPitchYaw(f32 p_pitch, f32 p_yaw)
	{
		pitch = p_pitch;
		yaw = p_yaw;

		// Clamp pitch to avoid gimbal lock
		pitch = Math::Clamp(pitch, -Math::Radians(89.0f), Math::Radians(89.0f));

		Quat pitchRotation = Quat::AngleAxis(pitch, Vec3{1.0f, 0.0f, 0.0f});
		Quat yawRotation = Quat::AngleAxis(yaw, Vec3{0.0f, 1.0f, 0.0f});
		rotation = Quat::Normalized(pitchRotation * yawRotation);

		UpdateViewMatrix();
	}

	void FreeViewCamera::UpdateViewMatrix()
	{
		view = Quat::ToMatrix4(rotation) * Mat4::Translate(-position) * Mat4::Scale(scale);
		right = {view[0][0], view[1][0], view[2][0]};
		up = {view[0][1], view[1][1], view[2][1]};
		direction = {view[0][2], view[1][2], view[2][2]};
	}
}
