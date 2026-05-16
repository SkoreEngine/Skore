
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct AnimationClipHandler : ResourceAssetHandler
	{
		SK_CLASS(AnimationClipHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".animation";
		}

		void OpenAsset(RID asset) override
		{
			//TODO
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<AnimationClipResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Animation Clip";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_PERSON_RUNNING;
		}
	};

	void RegisterAnimationHandler()
	{
		Reflection::Type<AnimationClipHandler>();
	}
}
