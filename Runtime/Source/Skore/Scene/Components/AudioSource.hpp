#pragma once
#include "Skore/Audio/AudioCommon.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	struct AudioInstance;

	class SK_API AudioSource : public Component
	{
	public:
		SK_CLASS(AudioSource, Component);

		void Destroy() override;
		void OnStart() override;

		void SetAudioResource(RID audioResource);
		RID  GetAudioResource() const;

		void SetVolume(f32 volume);
		f32  GetVolume() const;
		void SetPitch(f32 pitch);
		f32  GetPitch() const;
		void SetStereoPan(f32 stereoPan);
		f32  GetStereoPan() const;
		void SetPlayOnStart(bool playOnStart);
		bool GetPlayOnStart() const;
		void SetLoop(bool loop);
		bool GetLoop() const;

		void             SetIs3D(bool is3D);
		bool             GetIs3D() const;
		void             SetAttenuationModel(AttenuationModel attenuationModel);
		AttenuationModel GetAttenuationModel() const;
		void             SetMinDistance(f32 minDistance);
		f32              GetMinDistance() const;
		void             SetMaxDistance(f32 maxDistance);
		f32              GetMaxDistance() const;
		void             SetRolloffFactor(f32 rolloffFactor);
		f32              GetRolloffFactor() const;
		void             SetDopplerFactor(f32 dopplerFactor);
		f32              GetDopplerFactor() const;

		void PlayAudio() const;
		void StopAudio() const;
		void PauseAudio() const;
		void ResumeAudio() const;

		static void RegisterType(NativeReflectType<AudioSource>& type);

	private:
		AudioInstance* m_instance = nullptr;

		TypedRID<AudioResource> m_audioResource = {};

		f32  m_volume = 1.0f;
		f32  m_pitch = 1.0f;
		f32  m_stereoPan = 0.0f;
		bool m_playOnStart = false;
		bool m_loop = false;
		bool m_is3D = false;

		f32 m_minDistance = 1.0f;
		f32 m_maxDistance = 500.0f;
		f32 m_rolloffFactor = 1.0f;
		f32 m_dopplerFactor = 1.0f;
		AttenuationModel m_attenuationModel = AttenuationModel::Linear;

		void CreateAudioInstance();
	};
}
