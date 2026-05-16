
#include "Skore/Editor.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Script/ScriptCommon.hpp"
#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{

	static Logger& logger = Logger::GetLogger("Skore::LuaScriptHandler");

	struct LuaScriptHandler : ResourceAssetHandler
	{
		SK_CLASS(LuaScriptHandler, ResourceAssetHandler);

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<LuaScriptResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Lua Script";
		}

		StringView Extension() override
		{
			return ".lua";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_FILE_CODE;
		}

		int GetLoadOrder() const override
		{
			return 0;
		}

		void Reloaded(RID asset, StringView absolutePath) override
		{
			String source = FileSystem::ReadFileAsString(absolutePath);
			logger.Debug("reloading {} source size {} ", absolutePath, source.Size());
			ScriptManager::LoadScript(source, ".lua");
		}

		RID Load(RID asset, StringView absolutePath) override
		{
			ScriptManager::LoadScript(FileSystem::ReadFileAsString(absolutePath), ".lua");
			return Resources::Create<LuaScriptResource>();
		}

		void Save(RID object, StringView absolutePath) override
		{
			if (!FileSystem::GetFileStatus(absolutePath).exists)
			{
				FileSystem::SaveFileAsString(absolutePath, "-- Lua Script\nprint(\"Hello from Skore!\")\n");
			}
		}

		void OpenAsset(RID asset) override
		{
			StringView absolutePath = ResourceAssets::GetAbsolutePath(asset);
			if (FileSystem::GetFileStatus(absolutePath).exists)
			{
				Platform::OpenURL(absolutePath.CStr());
			}
			else
			{
				Editor::ShowErrorDialog("Please save your script file before opening it in an external tool.");
			}
		}

		void Export(RID object, ArchiveWriter& writer) override
		{
			StringView absolutePath = ResourceAssets::GetAbsolutePath(object);
			String source = FileSystem::ReadFileAsString(absolutePath);
			ByteBuffer buffer = ScriptManager::BuildScript(source, Extension());

			RID rid = Resources::Create<LuaScriptResource>(UUID::RandomUUID());

			ResourceObject exportedObject = Resources::Write(rid);
			exportedObject.SetBlob(LuaScriptResource::Binary, buffer);
			exportedObject.Commit();

			Resources::Serialize(rid, writer);

			Resources::Destroy(rid);
		}
	};

	void RegisterLuaScriptHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Lua Script", .icon = ICON_FA_CODE,
			.priority = 40,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<LuaScriptResource>::ID()
		});

		Reflection::Type<LuaScriptHandler>();
	}
}
