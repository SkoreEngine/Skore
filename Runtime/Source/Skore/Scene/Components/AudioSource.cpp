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

#include "AudioSource.hpp"

#include "Skore/Audio/AudioEngine.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"


namespace Skore
{
	void AudioSource::OnStart()
	{
		CreateAudioInstance();
	}

	void AudioSource::CreateAudioInstance()
	{
		if (m_instance)
		{
			AudioEngine::DestroyInstance(m_instance);
		}
		m_instance = nullptr;

		if (m_audioResource)
		{
			m_instance = AudioEngine::CreateInstance(m_audioResource);

			AudioEngine::SetVolume(m_instance, m_volume);
			AudioEngine::SetPitch(m_instance, m_pitch);
			AudioEngine::SetPan(m_instance, m_stereoPan);
			AudioEngine::SetLooping(m_instance, m_loop);
			AudioEngine::SetIs3D(m_instance, m_is3D);

			AudioEngine::SetPosition(m_instance, entity->GetWorldPosition());
			AudioEngine::SetAttenuationModel(m_instance, m_attenuationModel);
			AudioEngine::SetMinDistance(m_instance, m_minDistance);
			AudioEngine::SetMaxDistance(m_instance, m_maxDistance);
			AudioEngine::SetRolloffFactor(m_instance, m_rolloffFactor);
			AudioEngine::SetDopplerFactor(m_instance, m_dopplerFactor);


			if (m_playOnStart)
			{
				AudioEngine::StartAudio(m_instance);
			}
		}
	}

	void AudioSource::SetAudioResource(RID audioResource)
	{
		bool changed = m_audioResource != audioResource;
		m_audioResource = audioResource;
		if (m_instance && changed)
		{
			CreateAudioInstance();
		}
	}

	RID AudioSource::GetAudioResource() const
	{
		return m_audioResource;
	}

	void AudioSource::SetVolume(f32 volume)
	{
		m_volume = volume;
		if (m_instance)
		{
			AudioEngine::SetVolume(m_instance, volume);
		}
	}

	f32 AudioSource::GetVolume() const
	{
		return m_volume;
	}

	void AudioSource::SetPitch(f32 pitch)
	{
		m_pitch = pitch;
		if (m_instance)
		{
			AudioEngine::SetPitch(m_instance, pitch);
		}
	}

	f32 AudioSource::GetPitch() const
	{
		return m_pitch;
	}

	void AudioSource::SetStereoPan(f32 stereoPan)
	{
		m_stereoPan = stereoPan;
		if (m_instance)
		{
			AudioEngine::SetPan(m_instance, stereoPan);
		}
	}

	f32 AudioSource::GetStereoPan() const
	{
		return m_stereoPan;
	}

	void AudioSource::SetPlayOnStart(bool playOnStart)
	{
		m_playOnStart = playOnStart;
	}

	bool AudioSource::GetPlayOnStart() const
	{
		return m_playOnStart;
	}

	void AudioSource::SetLoop(bool loop)
	{
		m_loop = loop;

		if (m_instance)
		{
			AudioEngine::SetLooping(m_instance, loop);
		}
	}

	bool AudioSource::GetLoop() const
	{
		return m_loop;
	}

	void AudioSource::SetIs3D(bool is3D)
	{
		m_is3D = is3D;
		if (m_instance)
		{
			AudioEngine::SetIs3D(m_instance, is3D);
		}
	}

	bool AudioSource::GetIs3D() const
	{
		return m_is3D;
	}

	void AudioSource::SetAttenuationModel(AttenuationModel attenuationModel)
	{
		m_attenuationModel = attenuationModel;
		if (m_instance)
		{
			AudioEngine::SetAttenuationModel(m_instance, attenuationModel);
		}
	}

	AttenuationModel AudioSource::GetAttenuationModel() const
	{
		return m_attenuationModel;
	}

	void AudioSource::SetMinDistance(f32 minDistance)
	{
		m_minDistance = minDistance;
		if (m_instance)
		{
			AudioEngine::SetMinDistance(m_instance, minDistance);
		}
	}

	f32 AudioSource::GetMinDistance() const
	{
		return m_minDistance;
	}

	void AudioSource::SetMaxDistance(f32 maxDistance)
	{
		m_maxDistance = maxDistance;
		if (m_instance)
		{
			AudioEngine::SetMaxDistance(m_instance, maxDistance);
		}
	}

	f32 AudioSource::GetMaxDistance() const
	{
		return m_maxDistance;
	}

	void AudioSource::SetRolloffFactor(f32 rolloffFactor)
	{
		m_rolloffFactor = rolloffFactor;
		if (m_instance)
		{
			AudioEngine::SetRolloffFactor(m_instance, rolloffFactor);
		}
	}

	f32 AudioSource::GetRolloffFactor() const
	{
		return m_rolloffFactor;
	}

	void AudioSource::SetDopplerFactor(f32 dopplerFactor)
	{
		m_dopplerFactor = dopplerFactor;
		if (m_instance)
		{
			AudioEngine::SetDopplerFactor(m_instance, dopplerFactor);
		}
	}

	f32 AudioSource::GetDopplerFactor() const
	{
		return m_dopplerFactor;
	}

	void AudioSource::PlayAudio() const
	{
		if (m_instance)
		{
			AudioEngine::StartAudio(m_instance);
		}
	}

	void AudioSource::StopAudio() const
	{
		if (m_instance)
		{
			AudioEngine::StopAudio(m_instance);
		}
	}

	void AudioSource::PauseAudio() const
	{
		if (m_instance)
		{
			AudioEngine::PauseAudio(m_instance);
		}
	}

	void AudioSource::ResumeAudio() const
	{
		if (m_instance)
		{
			AudioEngine::ResumeAudio(m_instance);
		}
	}


	void AudioSource::Destroy()
	{
		if (m_instance)
		{
			AudioEngine::DestroyInstance(m_instance);
		}
	}

	void AudioSource::RegisterType(NativeReflectType<AudioSource>& type)
	{
		type.Field<&AudioSource::m_audioResource, &AudioSource::GetAudioResource, &AudioSource::SetAudioResource>("audioResource");
		type.Field<&AudioSource::m_volume, &AudioSource::GetVolume, &AudioSource::SetVolume>("volume");
		type.Field<&AudioSource::m_pitch, &AudioSource::GetPitch, &AudioSource::SetPitch>("pitch");
		type.Field<&AudioSource::m_stereoPan, &AudioSource::GetStereoPan, &AudioSource::SetStereoPan>("stereoPan");
		type.Field<&AudioSource::m_playOnStart, &AudioSource::GetPlayOnStart, &AudioSource::SetPlayOnStart>("playOnStart");
		type.Field<&AudioSource::m_loop, &AudioSource::GetLoop, &AudioSource::SetLoop>("loop");
		type.Field<&AudioSource::m_is3D, &AudioSource::GetIs3D, &AudioSource::SetIs3D>("is3D");
		type.Field<&AudioSource::m_attenuationModel, &AudioSource::GetAttenuationModel, &AudioSource::SetAttenuationModel>("attenuationModel");
		type.Field<&AudioSource::m_minDistance, &AudioSource::GetMinDistance, &AudioSource::SetMinDistance>("minDistance");
		type.Field<&AudioSource::m_maxDistance, &AudioSource::GetMaxDistance, &AudioSource::SetMaxDistance>("maxDistance");
		type.Field<&AudioSource::m_rolloffFactor, &AudioSource::GetRolloffFactor, &AudioSource::SetRolloffFactor>("rolloffFactor");
		type.Field<&AudioSource::m_dopplerFactor, &AudioSource::GetDopplerFactor, &AudioSource::SetDopplerFactor>("dopplerFactor");
	}
}
