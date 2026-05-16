#include "Skore/Audio/AudioEngine.hpp"
#include "Skore/Audio/AudioCommon.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"

#include "miniaudio.h"

namespace Skore
{
	namespace
	{
		ma_engine    engine;
		bool         engineEnabled = true;
		HashSet<RID> audioClips;
	}

	struct AudioInstance
	{
		ma_sound sound;
	};

	void AudioEngineInit()
	{
		ma_engine_config engineConfig = ma_engine_config_init();
		engineConfig.listenerCount = 1; // Number of listeners
		ma_engine_init(&engineConfig, &engine);
		ma_engine_start(&engine);
		engineEnabled = true;
	}

	void AudioEngineShutdown()
	{
		ma_engine_stop(&engine);
		ma_engine_uninit(&engine);
	}

	bool AudioEngine::IsSoundEnabled()
	{
		return engineEnabled;
	}

	void AudioEngine::SetSoundEnabled(bool value)
	{
		engineEnabled = value;

		if (!engineEnabled)
		{
			ma_engine_stop(&engine);
		}
		else
		{
			ma_engine_start(&engine);
		}
	}

	void AudioEngine::SetVolume(f32 volume)
	{
		ma_engine_set_volume(&engine, volume);
	}

	void AudioEngine::SetListenerActive(bool value)
	{
		ma_engine_listener_set_enabled(&engine, 0, value);
	}

	void AudioEngine::SetListenerPosition(const Vec3& pos)
	{
		ma_engine_listener_set_position(&engine, 0, pos.x, pos.y, pos.z);
	}

	void AudioEngine::SetListenerDirection(const Vec3& dir)
	{
		ma_engine_listener_set_direction(&engine, 0, dir.x, dir.y, dir.z);
	}

	void AudioEngine::SetListenerUp(const Vec3& up)
	{
		ma_engine_listener_set_world_up(&engine, 0, up.x, up.y, up.z);
	}

	AudioInstance* AudioEngine::CreateInstance(RID audioResource)
	{
		AudioInstance* instance = Alloc<AudioInstance>();

		char buffer[32];
		sprintf(buffer, "audio_%llu", audioResource.id);

		if (!audioClips.Has(audioResource))
		{
			ma_resource_manager* resourceManager = ma_engine_get_resource_manager(&engine);

			ResourceObject audioObject = Resources::Read(audioResource);
			Span<u8> audioClip = audioObject.GetBlob(AudioResource::Bytes);
			ma_resource_manager_register_encoded_data(resourceManager, buffer, audioClip.Data(), audioClip.Size());
		}

		ma_sound_init_from_file(&engine, buffer, 0, NULL, NULL, &instance->sound);

		return instance;
	}

	void AudioEngine::StartAudio(AudioInstance* instance)
	{
		ma_sound_start(&instance->sound);
	}

	void AudioEngine::StopAudio(AudioInstance* instance)
	{
		ma_sound_stop(&instance->sound);
	}

	void AudioEngine::PauseAudio(AudioInstance* instance)
	{
		//ma_sound_pause(&instance->sound);
	}

	void AudioEngine::ResumeAudio(AudioInstance* instance)
	{
		//ma_sound_resume(&instance->sound);
	}

	void AudioEngine::SetVolume(AudioInstance* instance, f32 value)
	{
		ma_sound_set_volume(&instance->sound, value);
	}
	void AudioEngine::SetPitch(AudioInstance* instance, f32 value)
	{
		ma_sound_set_pitch(&instance->sound, value);
	}

	void AudioEngine::SetLooping(AudioInstance* instance, bool value)
	{
		ma_sound_set_looping(&instance->sound, value);
	}

	void AudioEngine::SetPan(AudioInstance* instance, f32 value)
	{
		ma_sound_set_pan(&instance->sound, value);
	}

	void AudioEngine::SetPosition(AudioInstance* instance, const Vec3& position)
	{
		ma_sound_set_position(&instance->sound, position.x, position.y, position.z);
	}

	void AudioEngine::SetAttenuationModel(AudioInstance* instance, AttenuationModel model)
	{
		switch (model)
		{
			case AttenuationModel::Inverse:
				ma_sound_set_attenuation_model(&instance->sound, ma_attenuation_model_inverse);
				break;
			case AttenuationModel::Linear:
				ma_sound_set_attenuation_model(&instance->sound, ma_attenuation_model_linear);
				break;
			case AttenuationModel::Exponential:
				ma_sound_set_attenuation_model(&instance->sound, ma_attenuation_model_exponential);
				break;
		}
	}

	void AudioEngine::SetDopplerFactor(AudioInstance* instance, f32 value)
	{
		ma_sound_set_doppler_factor(&instance->sound, value);
	}

	void AudioEngine::SetRolloffFactor(AudioInstance* instance, f32 value)
	{
		ma_sound_set_rolloff(&instance->sound, value);
	}

	void AudioEngine::SetMaxDistance(AudioInstance* instance, f32 value)
	{
		ma_sound_set_max_distance(&instance->sound, value);
	}

	void AudioEngine::SetMinDistance(AudioInstance* instance, f32 value)
	{
		ma_sound_set_min_distance(&instance->sound, value);
	}

	void AudioEngine::SetIs3D(AudioInstance* instance, bool value)
	{
		ma_sound_set_spatialization_enabled(&instance->sound, value);
	}

	void AudioEngine::DestroyInstance(AudioInstance* instance)
	{
		ma_sound_uninit(&instance->sound);
		DestroyAndFree(instance);
	}
}
