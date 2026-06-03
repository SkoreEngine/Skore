#include "Skore/Utils/FreeViewCamera.hpp"

#include <cmath>

#include <SDL3/SDL.h>

#include "Skore/Core/Event.hpp"
#include "Skore/Events.hpp"
#include "Skore/IO/Input.hpp"

namespace Skore
{
	SK_API void AddSDLEventCallback(void (*callback)(SDL_Event* event, VoidPtr userData), VoidPtr userData);

	namespace
	{
		Vec2 cameraMouseRelative{};
		f32  cameraMouseWheel{};
		bool cameraInputRegistered = false;

		void CameraInputEndFrame()
		{
			cameraMouseRelative = {};
			cameraMouseWheel = {};
		}

		void CameraSDLEvent(SDL_Event* event, VoidPtr userData)
		{
			switch (event->type)
			{
				case SDL_EVENT_MOUSE_MOTION:
					cameraMouseRelative += Vec2{event->motion.xrel, event->motion.yrel};
					break;
				case SDL_EVENT_MOUSE_WHEEL:
					cameraMouseWheel += event->wheel.y;
					break;
			}
		}

		void RegisterCameraInput()
		{
			if (cameraInputRegistered) return;
			cameraInputRegistered = true;
			AddSDLEventCallback(CameraSDLEvent, nullptr);
			Event::Bind<OnEndFrame, CameraInputEndFrame>();
		}
	}

	FreeViewCamera::FreeViewCamera()
	{
		RegisterCameraInput();
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

			const bool* keyboard = SDL_GetKeyboardState(nullptr);

			Vec2 currentMouseOffset = cameraMouseRelative * 0.1f * sensitivity;

			f32 mouseAlpha = 1.0f - std::pow(smoothingFactor, dt * 60.0f);
			Vec2 smoothedMouseOffset;
			smoothedMouseOffset.x = previousMouseOffset.x + (currentMouseOffset.x - previousMouseOffset.x) * mouseAlpha;
			smoothedMouseOffset.y = previousMouseOffset.y + (currentMouseOffset.y - previousMouseOffset.y) * mouseAlpha;

			previousMouseOffset = smoothedMouseOffset;

			yaw += smoothedMouseOffset.x;
			pitch += smoothedMouseOffset.y;

			pitch = Math::Clamp(pitch, -Math::Radians(89.0f), Math::Radians(89.0f));

			Quat pitchRotation = Quat::AngleAxis(pitch, Vec3{1.0f, 0.0f, 0.0f});
			Quat yawRotation = Quat::AngleAxis(yaw, Vec3{0.0f, 1.0f, 0.0f});
			rotation = Quat::Normalized(pitchRotation * yawRotation);

			f32 wheel = cameraMouseWheel;
			if (wheel != 0.0f)
			{
				cameraSpeed *= (wheel > 0.0f) ? 1.1f : 0.9f;
				cameraSpeed = Math::Clamp(cameraSpeed, 0.1f, 500.0f);
			}

			Vec3 targetMovement{};
			f32 localCameraSpeed = cameraSpeed;

			if (keyboard[SDL_SCANCODE_LSHIFT])
			{
				localCameraSpeed *= 3.f;
			}

			if (keyboard[SDL_SCANCODE_A])
			{
				targetMovement += right * -localCameraSpeed;
			}
			if (keyboard[SDL_SCANCODE_D])
			{
				targetMovement += right * localCameraSpeed;
			}
			if (keyboard[SDL_SCANCODE_W])
			{
				targetMovement += direction * -localCameraSpeed;
			}
			if (keyboard[SDL_SCANCODE_S])
			{
				targetMovement += direction * localCameraSpeed;
			}
			if (keyboard[SDL_SCANCODE_E])
			{
				targetMovement += up * localCameraSpeed;
			}
			if (keyboard[SDL_SCANCODE_Q])
			{
				targetMovement += up * -localCameraSpeed;
			}

			f32 moveAlpha = 1.0f - std::pow(movementSmoothingFactor, dt * 60.0f);
			Vec3 smoothedMovement;
			smoothedMovement.x = previousMovement.x + (targetMovement.x - previousMovement.x) * moveAlpha;
			smoothedMovement.y = previousMovement.y + (targetMovement.y - previousMovement.y) * moveAlpha;
			smoothedMovement.z = previousMovement.z + (targetMovement.z - previousMovement.z) * moveAlpha;

			previousMovement = smoothedMovement;

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
