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

#include "AudioListener.hpp"

#include "Skore/Audio/AudioEngine.hpp"
#include "Skore/Scene/Entity.hpp"

namespace Skore
{
	void AudioListener::OnStart()
	{
		AudioEngine::SetListenerActive(true);
	}

	void AudioListener::Destroy()
	{
		AudioEngine::SetListenerActive(false);
	}

	void AudioListener::ProcessEvent(const EntityEventDesc& event)
	{
		switch (event.type)
		{
			case EntityEventType::EntityActivated:
				break;
			case EntityEventType::EntityDeactivated:
				break;
			case EntityEventType::TransformUpdated:
				AudioEngine::SetListenerPosition(entity->GetWorldPosition());
				AudioEngine::SetListenerDirection(Math::GetForwardVector(entity->GetGlobalTransform()));
				AudioEngine::SetListenerUp(Math::GetUpVector(entity->GetGlobalTransform()));

				break;
		}
	}
}
