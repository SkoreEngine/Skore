#pragma once

#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore {
	class CameraObject;
}

namespace Skore
{
	class SK_API Camera : public Component
	{
	public:
		SK_CLASS(Camera, Component);

		void Create(ComponentSettings& settings) override;
		void Destroy() override;


		Projection GetProjection() const;
		void       SetProjection(Projection projection);
		f32        GetFov() const;
		void       SetFov(f32 fov);
		f32        GetNear() const;
		void       SetNear(f32 near);
		f32        GetFar() const;
		void       SetFar(f32 far);
		u64        GetCullingMask() const;
		void       SetCullingMask(u64 cullingMask);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<Camera>& type);

	private:
		Projection m_projection = Projection::Perspective;
		f32        m_fov = 60;
		f32        m_near = 0.1;
		f32        m_far = 1000.f;
		u64        m_cullingMask = ~0ULL;

		CameraObject* cameraObject = nullptr;
	};
}
