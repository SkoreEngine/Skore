#include "Skore/Scene/Components/AudioListener.hpp"

#include "Skore/Scene/Components/Transform.hpp"
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
			{
				AudioEngine::SetListenerPosition(entity->GetWorldPosition());
				AudioEngine::SetListenerDirection(Mat4::GetForwardVector(entity->GetWorldTransform()));
				AudioEngine::SetListenerUp(Mat4::GetUpVector(entity->GetWorldTransform()));
				break;
			}
		}
	}
}