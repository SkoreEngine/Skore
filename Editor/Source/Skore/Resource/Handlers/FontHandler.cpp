
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct FontHandler : ResourceAssetHandler
	{
		SK_CLASS(FontHandler, ResourceAssetHandler);

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<FontResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Font";
		}

		StringView Extension() override
		{
			return ".font";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_FONT;
		}

		void OpenAsset(RID asset) override {}
	};

	void RegisterFontHandler()
	{
		Reflection::Type<FontHandler>();
	}
}
