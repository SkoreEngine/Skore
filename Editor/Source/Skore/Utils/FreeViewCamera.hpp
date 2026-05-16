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

		void SetPosition(Vec3 position);
		void SetPitchYaw(f32 p_pitch, f32 p_yaw);

		Quat GetRotation() const
		{
			return rotation;
		}

		Vec3 GetScale() const
		{
			return scale;
		}

		void SetSensitivity(f32 p_sensitivity)
		{
			sensitivity = p_sensitivity;
		}

		// Smoothing parameters (valid range: 0.0 to < 1.0, tuned at 60fps reference rate)
		f32 smoothingFactor = 0.7f;
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
		f32 sensitivity = 0.01f;
		
		Vec2 previousMouseOffset{};
		Vec3 previousMovement{};

		void UpdateViewMatrix();
	};
}
