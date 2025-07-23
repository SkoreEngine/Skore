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
