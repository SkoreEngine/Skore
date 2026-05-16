#pragma once
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	class SK_API AudioListener : public Component
	{
	public:
		SK_CLASS(AudioListener, Component);

		void OnStart() override;
		void Destroy() override;

		void ProcessEvent(const EntityEventDesc& event) override;

	private:

	};
}
