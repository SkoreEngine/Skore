
#include "Skore/Audio/AudioCommon.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct AudioHandler : ResourceAssetHandler
	{
		SK_CLASS(AudioHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".audio";
		}

		void OpenAsset(RID asset) override
		{
			//TODO
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<AudioResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Audio Clip";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_FILE_AUDIO;
		}
	};

	void RegisterAudioHandler()
	{
		Reflection::Type<AudioHandler>();
	}
}
