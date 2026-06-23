#include "Skore/EditorSettings.hpp"

#include "Skore/Core/Settings.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		RID loadedEditorSettings;
	}

	String GetEditorSettingsPath()
	{
		return Path::Join(FileSystem::AppFolder(), "Skore", "EditorSettings.cfg");
	}

	RID LoadEditorSettingsResource()
	{
		if (loadedEditorSettings)
		{
			return loadedEditorSettings;
		}

		String editorSettingsPath = GetEditorSettingsPath();
		if (!FileSystem::GetFileStatus(editorSettingsPath).exists)
		{
			YamlArchiveWriter writer;
			Settings::CreateDefault(writer, TypeInfo<EditorSettings>::ID());
			FileSystem::SaveFileAsString(editorSettingsPath, writer.EmitAsString());
		}

		YamlArchiveReader reader(FileSystem::ReadFileAsString(editorSettingsPath));
		loadedEditorSettings = Settings::Load(reader, TypeInfo<EditorSettings>::ID());
		return loadedEditorSettings;
	}

	void SaveEditorSettingsResource()
	{
		YamlArchiveWriter writer;
		Settings::Save(writer, TypeInfo<EditorSettings>::ID());
		FileSystem::SaveFileAsString(GetEditorSettingsPath(), writer.EmitAsString());
	}

	bool ShouldLoadPreviousProjectOnStartup()
	{
		RID editorSettings = LoadEditorSettingsResource();
		if (!editorSettings)
		{
			return true;
		}

		u64 version = Resources::GetVersion(editorSettings);
		RID generalSettings = Settings::Get<EditorSettings, GeneralEditorSettings>();
		bool loadPreviousProject = true;
		if (generalSettings)
		{
			if (ResourceObject settings = Resources::Read(generalSettings))
			{
				loadPreviousProject = settings.GetBool(GeneralEditorSettings::LoadPreviousProjectOnStartup);
			}
		}

		if (version != Resources::GetVersion(editorSettings))
		{
			SaveEditorSettingsResource();
		}

		return loadPreviousProject;
	}

	void RegisterEditorSettingsTypes()
	{
		ResourceType* type = Resources::Type<GeneralEditorSettings>()
			.Field<GeneralEditorSettings::LoadPreviousProjectOnStartup>(ResourceFieldType::Bool)
			.Attribute<EditableSettings>(EditableSettings{
				.path = "Editor/General",
				.type = TypeInfo<EditorSettings>::ID(),
				.order = 0
			})
			.Build()
			.GetResourceType();

		RID rid = Resources::Create<GeneralEditorSettings>();
		ResourceObject settings = Resources::Write(rid);
		settings.SetBool(GeneralEditorSettings::LoadPreviousProjectOnStartup, true);
		settings.Commit();
		type->SetDefaultValue(rid);
	}
}
