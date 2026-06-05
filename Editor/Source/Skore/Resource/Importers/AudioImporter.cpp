#include "Skore/Audio/AudioCommon.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	struct AudioImporter : ResourceAssetImporter
	{
		SK_CLASS(AudioImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".wav", ".mp3", ".ogg", ".flac"};
		}

		StringView OutputExtension() override
		{
			return ".audio";
		}

		void Ingest(IngestContext& ctx) override
		{
			ctx.DeclareSubResource("main", TypeInfo<AudioResource>::ID());
		}

		void Cook(CookContext& ctx) override
		{
			String name = "audio";
			if (ResourceObject wrapper = Resources::Read(ctx.importedAsset))
			{
				name = Path::Name(wrapper.GetString(ResourceImportedAsset::OriginalFileName));
			}

			RID audio = ctx.SubResource("main", TypeInfo<AudioResource>::ID());
			ResourceObject audioObject = Resources::Write(audio);
			audioObject.SetString(AudioResource::Name, name);
			audioObject.SetBlob(AudioResource::Bytes, ctx.sourceBytes);
			audioObject.Commit(ctx.scope);
		}
	};

	void RegisterAudioImporter()
	{
		Reflection::Type<AudioImporter>();
	}
}
