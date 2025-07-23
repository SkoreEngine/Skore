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
