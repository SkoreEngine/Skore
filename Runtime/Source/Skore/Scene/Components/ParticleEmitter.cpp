#include "Skore/Scene/Components/ParticleEmitter.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void ParticleEmitter::Create(ComponentSettings& settings)
	{
		settings.enableUpdate = true;

		m_particleObject = scene->renderObjects.CreateParticleObject();
		m_particleObject->SetMaxParticles(m_maxParticles);
		m_particleObject->SetVisible(true);

		m_particles.Resize(m_maxParticles);
		for (u32 i = 0; i < m_maxParticles; i++)
		{
			m_particles[i] = {};
			m_particles[i].alive = 0.0f;
		}

		m_particleObject->UploadData(m_particles.Data(), m_maxParticles * sizeof(GPUParticle));
	}

	void ParticleEmitter::Destroy()
	{
		if (m_particleObject)
		{
			m_particleObject->Destroy();
			m_particleObject = nullptr;
		}
		m_particles.Clear();
	}

	void ParticleEmitter::Tick()
	{
		if (!m_particleObject || !m_particleObject->GetVisible()) return;

		f32 dt = static_cast<f32>(App::DeltaTime());

		// --- Emit ---
		bool canEmit = m_looping || m_elapsedTime < m_duration;
		m_elapsedTime += dt;

		u32 emitCount = 0;
		if (canEmit)
		{
			m_emitAccumulator += m_emissionRate * dt;
			emitCount = static_cast<u32>(m_emitAccumulator);
			m_emitAccumulator -= static_cast<f32>(emitCount);
		}

		Vec3 emitterPos = Mat4::GetTranslation(entity->GetWorldTransform());
		Vec4 startColorVec = m_startColor.ToVec4();

		for (u32 i = 0; i < emitCount; i++)
		{
			// Find a dead particle slot (round-robin search)
			bool found = false;
			for (u32 j = 0; j < m_maxParticles; j++)
			{
				u32 idx = (m_searchIndex + j) % m_maxParticles;
				if (m_particles[idx].alive < 0.5f)
				{
					GPUParticle& p = m_particles[idx];

					Vec3 r = Vec3(
						Random::NextFloat32(-m_emitRadius, m_emitRadius),
						Random::NextFloat32(-m_emitRadius, m_emitRadius),
						Random::NextFloat32(-m_emitRadius, m_emitRadius)
					);
					p.position = emitterPos +r;

					p.velocity = Vec3(
						Random::NextFloat32(m_initialVelocityMin.x, m_initialVelocityMax.x),
						Random::NextFloat32(m_initialVelocityMin.y, m_initialVelocityMax.y),
						Random::NextFloat32(m_initialVelocityMin.z, m_initialVelocityMax.z)
					);
					p.age = 0.0f;
					p.lifetime = m_particleLifetime;
					p.color = startColorVec;
					p.size = m_startSize;
					p.alive = 1.0f;
					p.pad[0] = 0.0f;
					p.pad[1] = 0.0f;

					m_searchIndex = (idx + 1) % m_maxParticles;
					found = true;
					break;
				}
			}
			if (!found) break; // All slots full
		}

		// --- Update ---
		for (u32 i = 0; i < m_maxParticles; i++)
		{
			GPUParticle& p = m_particles[i];
			if (p.alive < 0.5f) continue;

			p.age += dt;
			if (p.age >= p.lifetime)
			{
				p.alive = 0.0f;
				continue;
			}

			p.velocity.y -= m_gravity * dt;
			p.position = p.position + p.velocity * dt;

			f32 t = p.age / p.lifetime;
			p.size = m_startSize + (m_endSize - m_startSize) * t;
			p.color.w = startColorVec.w * (1.0f - t);
		}

		// --- Upload to GPU ---
		m_particleObject->UploadData(m_particles.Data(), m_maxParticles * sizeof(GPUParticle));
	}

	void ParticleEmitter::ProcessEvent(const EntityEventDesc& event)
	{
		if (!m_particleObject) return;

		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				m_particleObject->SetVisible(true);
				break;
			case EntityEventType::EntityDeactivated:
				m_particleObject->SetVisible(false);
				break;
			case EntityEventType::EntityIsSelectedOnEditor:
				Tick();
				break;
		}
	}

	void ParticleEmitter::OnUpdate(f64 deltaTime)
	{
		Tick();
	}

	void ParticleEmitter::SetMaxParticles(u32 maxParticles)
	{
		if (m_maxParticles == maxParticles) return;

		m_maxParticles = maxParticles;
		m_particles.Resize(m_maxParticles);
		for (u32 i = 0; i < m_maxParticles; i++)
		{
			m_particles[i] = {};
			m_particles[i].alive = 0.0f;
		}
		m_searchIndex = 0;
		m_emitAccumulator = 0.0f;

		if (m_particleObject)
		{
			m_particleObject->SetMaxParticles(maxParticles);
			m_particleObject->UploadData(m_particles.Data(), m_maxParticles * sizeof(GPUParticle));
		}
	}

	u32 ParticleEmitter::GetMaxParticles() const { return m_maxParticles; }

	void ParticleEmitter::SetEmissionRate(f32 rate)
	{
		m_emissionRate = rate;
	}

	f32 ParticleEmitter::GetEmissionRate() const { return m_emissionRate; }

	void ParticleEmitter::SetParticleLifetime(f32 lifetime)
	{
		m_particleLifetime = lifetime;
	}

	f32 ParticleEmitter::GetParticleLifetime() const { return m_particleLifetime; }

	void ParticleEmitter::SetEmitRadius(f32 radius)
	{
		m_emitRadius = radius;
	}

	f32 ParticleEmitter::GetEmitRadius() const { return m_emitRadius; }

	void ParticleEmitter::SetStartColor(const Color& color)
	{
		m_startColor = color;
	}

	const Color& ParticleEmitter::GetStartColor() const { return m_startColor; }

	void ParticleEmitter::SetStartSize(f32 size)
	{
		m_startSize = size;
	}

	f32 ParticleEmitter::GetStartSize() const { return m_startSize; }

	void ParticleEmitter::SetEndSize(f32 size)
	{
		m_endSize = size;
	}

	f32 ParticleEmitter::GetEndSize() const { return m_endSize; }

	void ParticleEmitter::SetInitialVelocityMin(const Vec3& vel)
	{
		m_initialVelocityMin = vel;
	}

	const Vec3& ParticleEmitter::GetInitialVelocityMin() const { return m_initialVelocityMin; }

	void ParticleEmitter::SetInitialVelocityMax(const Vec3& vel)
	{
		m_initialVelocityMax = vel;
	}

	const Vec3& ParticleEmitter::GetInitialVelocityMax() const { return m_initialVelocityMax; }

	void ParticleEmitter::SetGravity(f32 gravity)
	{
		m_gravity = gravity;
	}

	f32 ParticleEmitter::GetGravity() const { return m_gravity; }

	void ParticleEmitter::SetLooping(bool looping) { m_looping = looping; }
	bool ParticleEmitter::GetLooping() const { return m_looping; }

	void ParticleEmitter::SetDuration(f32 duration) { m_duration = duration; }
	f32  ParticleEmitter::GetDuration() const { return m_duration; }

	bool ParticleEmitter::IsFinished() const
	{
		if (m_looping) return false;
		return m_elapsedTime >= m_duration + m_particleLifetime;
	}

	void ParticleEmitter::RegisterType(NativeReflectType<ParticleEmitter>& type)
	{
		type.Field<&ParticleEmitter::m_maxParticles, &ParticleEmitter::GetMaxParticles, &ParticleEmitter::SetMaxParticles>("maxParticles");
		type.Field<&ParticleEmitter::m_emissionRate, &ParticleEmitter::GetEmissionRate, &ParticleEmitter::SetEmissionRate>("emissionRate");
		type.Field<&ParticleEmitter::m_particleLifetime, &ParticleEmitter::GetParticleLifetime, &ParticleEmitter::SetParticleLifetime>("particleLifetime");
		type.Field<&ParticleEmitter::m_emitRadius, &ParticleEmitter::GetEmitRadius, &ParticleEmitter::SetEmitRadius>("emitRadius");
		type.Field<&ParticleEmitter::m_startColor, &ParticleEmitter::GetStartColor, &ParticleEmitter::SetStartColor>("startColor");
		type.Field<&ParticleEmitter::m_startSize, &ParticleEmitter::GetStartSize, &ParticleEmitter::SetStartSize>("startSize");
		type.Field<&ParticleEmitter::m_endSize, &ParticleEmitter::GetEndSize, &ParticleEmitter::SetEndSize>("endSize");
		type.Field<&ParticleEmitter::m_initialVelocityMin, &ParticleEmitter::GetInitialVelocityMin, &ParticleEmitter::SetInitialVelocityMin>("initialVelocityMin");
		type.Field<&ParticleEmitter::m_initialVelocityMax, &ParticleEmitter::GetInitialVelocityMax, &ParticleEmitter::SetInitialVelocityMax>("initialVelocityMax");
		type.Field<&ParticleEmitter::m_gravity, &ParticleEmitter::GetGravity, &ParticleEmitter::SetGravity>("gravity");
		type.Field<&ParticleEmitter::m_looping, &ParticleEmitter::GetLooping, &ParticleEmitter::SetLooping>("looping");
		type.Field<&ParticleEmitter::m_duration, &ParticleEmitter::GetDuration, &ParticleEmitter::SetDuration>("duration");
		type.Function<&ParticleEmitter::IsFinished>("IsFinished");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Effects"});
	}
}