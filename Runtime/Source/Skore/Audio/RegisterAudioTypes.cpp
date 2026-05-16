#include "Skore/Audio/AudioCommon.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	void RegisterAudioTypes()
	{
		auto attenuationModel = Reflection::Type<AttenuationModel>();
		attenuationModel.Value<AttenuationModel::Inverse>();
		attenuationModel.Value<AttenuationModel::Linear>();
		attenuationModel.Value<AttenuationModel::Exponential>();


		Resources::Type<AudioResource>()
			.Field(AudioResource::Name, "name", ResourceFieldType::String)
			.Field(AudioResource::Bytes, "bytes", ResourceFieldType::Blob)
			.Build();
	}
}