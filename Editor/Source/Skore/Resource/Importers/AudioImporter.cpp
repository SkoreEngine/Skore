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

		bool ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) override
		{
			RID audio = ResourceAssets::CreateImportedAsset(directory, TypeInfo<AudioResource>::ID(), Path::Name(path), scope, path);

			Array<u8> data;
			FileSystem::ReadFileAsByteArray(path, data);

			ResourceObject audioObject = Resources::Write(audio);
			audioObject.SetString(AudioResource::Name, Path::Name(path));
			audioObject.SetBlob(AudioResource::Bytes, data);
			audioObject.Commit(scope);

			return true;
		}
	};

	void RegisterAudioImporter()
	{
		Reflection::Type<AudioImporter>();
	}
}
