#pragma once

#include "Skore/Core/Color.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	struct GPUParticle
	{
		Vec3 position;
		f32  age;
		Vec3 velocity;
		f32  lifetime;
		Vec4 color;
		f32  size;
		f32  alive;
		f32  pad[2];
	};

	class SK_API ParticleEmitter : public Component
	{
	public:
		SK_CLASS(ParticleEmitter, Component);

		void Create(ComponentSettings& settings) override;
		void Destroy() override;
		void ProcessEvent(const EntityEventDesc& event) override;

		void OnUpdate(f64 deltaTime) override;

		void SetMaxParticles(u32 maxParticles);
		u32  GetMaxParticles() const;

		void SetEmissionRate(f32 rate);
		f32  GetEmissionRate() const;

		void SetParticleLifetime(f32 lifetime);
		f32  GetParticleLifetime() const;

		void SetEmitRadius(f32 radius);
		f32  GetEmitRadius() const;

		void         SetStartColor(const Color& color);
		const Color& GetStartColor() const;

		void SetStartSize(f32 size);
		f32  GetStartSize() const;

		void SetEndSize(f32 size);
		f32  GetEndSize() const;

		void        SetInitialVelocityMin(const Vec3& vel);
		const Vec3& GetInitialVelocityMin() const;

		void        SetInitialVelocityMax(const Vec3& vel);
		const Vec3& GetInitialVelocityMax() const;

		void SetGravity(f32 gravity);
		f32  GetGravity() const;

		void SetLooping(bool looping);
		bool GetLooping() const;

		void SetDuration(f32 duration);
		f32  GetDuration() const;

		void Tick();
		bool IsFinished() const;

		static void RegisterType(NativeReflectType<ParticleEmitter>& type);

	private:

		ParticleObject* m_particleObject = nullptr;
		f32             m_emitAccumulator = 0.0f;
		u32             m_searchIndex = 0;
		f32             m_elapsedTime = 0.0f;

		Array<GPUParticle> m_particles;

		bool  m_looping = true;
		f32   m_duration = 2.0f;
		u32   m_maxParticles = 10000;
		f32   m_emissionRate = 100.0f;
		f32   m_particleLifetime = 2.0f;
		f32   m_emitRadius = 0.5f;
		Color m_startColor = Color::WHITE;
		f32   m_startSize = 0.2f;
		f32   m_endSize = 0.0f;
		Vec3  m_initialVelocityMin = Vec3(-0.5f, 1.0f, -0.5f);
		Vec3  m_initialVelocityMax = Vec3(0.5f, 3.0f, 0.5f);
		f32   m_gravity = 9.81f;
	};
}
