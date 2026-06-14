#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "../../../../../Runtime/Source/Skore/UI/RmlUI.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{
	struct RmlUiHandler : ResourceAssetHandler
	{
		SK_CLASS(RmlUiHandler, ResourceAssetHandler);

		static String InfoPath(StringView absolutePath)
		{
			return String(absolutePath) + ".info";
		}

		void OpenAsset(RID asset) override
		{
			Platform::OpenURL(ResourceAssets::GetAbsolutePath(asset).CStr());
		}

		void AfterMove(RID asset, StringView oldAbsolutePath, StringView newAbsolutePath) override
		{
			String oldInfo = InfoPath(oldAbsolutePath);
			if (FileSystem::GetFileStatus(oldInfo).exists)
			{
				FileSystem::Rename(oldInfo, InfoPath(newAbsolutePath));
			}
		}

		RID Load(RID asset, StringView absolutePath) override
		{
			RID    resource;
			String infoPath = InfoPath(absolutePath);

			if (FileSystem::GetFileStatus(infoPath).exists)
			{
				YamlArchiveReader reader(FileSystem::ReadFileAsString(infoPath));
				resource = Resources::Deserialize(reader);
			}

			if (!resource)
			{
				resource = Resources::Create(GetResourceTypeId(), UUID::RandomUUID());
			}

			ResourceObject object = Resources::Write(resource);
			object.SetString(UIDocumentResource::Name, Path::Name(absolutePath));
			object.SetString(UIDocumentResource::Content, FileSystem::ReadFileAsString(absolutePath));
			object.Commit();

			return resource;
		}

		void Save(RID asset, StringView absolutePath) override
		{
			ResourceObject object = Resources::Read(asset);

			FileSystem::SaveFileAsString(absolutePath, object.GetString(UIDocumentResource::Content));

			YamlArchiveWriter writer;
			writer.BeginSeq("objects");
			writer.BeginMap();
			writer.WriteString("_uuid", Resources::GetUUID(asset).ToString());
			writer.WriteString("_type", Resources::GetType(asset)->GetName());
			writer.EndMap();
			writer.EndSeq();
			FileSystem::SaveFileAsString(InfoPath(absolutePath), writer.EmitAsString());
		}

		RID Create(UUID uuid, UndoRedoScope* scope) override
		{
			RID            asset = Resources::Create(GetResourceTypeId(), uuid, scope);
			ResourceObject object = Resources::Write(asset);
			object.SetString(UIDocumentResource::Content, DefaultContent());
			object.Commit(scope);
			return asset;
		}

		virtual StringView DefaultContent() = 0;
	};

	struct RmlUiDocumentHandler : RmlUiHandler
	{
		SK_CLASS(RmlUiDocumentHandler, RmlUiHandler);

		StringView Extension() override
		{
			return ".rml";
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<UIDocumentResource>::ID();
		}

		StringView GetDesc() override
		{
			return "UI Document";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_FILE_CODE;
		}

		StringView DefaultContent() override
		{
			return "<rml>\n"
				"<head>\n"
				"</head>\n"
				"<body>\n"
				"</body>\n"
				"</rml>\n";
		}
	};

	struct RmlUiStyleHandler : RmlUiHandler
	{
		SK_CLASS(RmlUiStyleHandler, RmlUiHandler);

		StringView Extension() override
		{
			return ".rcss";
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<UIStyleResource>::ID();
		}

		StringView GetDesc() override
		{
			return "UI Style";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_PALETTE;
		}

		StringView DefaultContent() override
		{
			return "body\n"
				"{\n"
				"}\n";
		}
	};

	void RegisterRmlUiHandler()
	{
		Reflection::Type<RmlUiHandler>();
		Reflection::Type<RmlUiDocumentHandler>();
		Reflection::Type<RmlUiStyleHandler>();

		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/UI",
			.icon = ICON_FA_ROCKET,
			.priority = 30
		});

		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/UI/Create Document",
			.icon = ICON_FA_FILE_CODE,
			.priority = 10,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<UIDocumentResource>::ID()
		});

		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/UI/Create Style",
			.icon = ICON_FA_PALETTE,
			.priority = 20,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<UIStyleResource>::ID()
		});
	}
}
