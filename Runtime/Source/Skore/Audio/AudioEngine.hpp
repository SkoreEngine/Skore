#pragma once
#include "AudioCommon.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	struct AudioInstance;

	struct SK_API AudioEngine
	{
		static bool IsSoundEnabled();
		static void SetSoundEnabled(bool value);
		static void SetVolume(f32 volume);

		static void SetListenerActive(bool value);
		static void SetListenerPosition(const Vec3& pos);
		static void SetListenerDirection(const Vec3& dir);
		static void SetListenerUp(const Vec3& up);


		static AudioInstance* CreateInstance(RID audioResource);
		static void           DestroyInstance(AudioInstance* instance);

		static void StartAudio(AudioInstance* instance);
		static void StopAudio(AudioInstance* instance);
		static void PauseAudio(AudioInstance* instance);
		static void ResumeAudio(AudioInstance* instance);

		static void SetVolume(AudioInstance* instance, f32 value);
		static void SetPitch(AudioInstance* instance, f32 value);
		static void SetLooping(AudioInstance* instance, bool value);
		static void SetPan(AudioInstance* instance, f32 value);

		//3D
		static void SetIs3D(AudioInstance* instance, bool value);
		static void SetPosition(AudioInstance* instance, const Vec3& position);
		static void SetAttenuationModel(AudioInstance* instance, AttenuationModel model);
		static void SetDopplerFactor(AudioInstance* instance, f32 value);
		static void SetRolloffFactor(AudioInstance* instance, f32 value);
		static void SetMaxDistance(AudioInstance* instance, f32 value);
		static void SetMinDistance(AudioInstance* instance, f32 value);


	};
}
