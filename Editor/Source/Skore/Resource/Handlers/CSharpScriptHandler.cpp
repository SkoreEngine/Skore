#include <cstring>
#include <mutex>

#include "Skore/Editor.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Utils/ProjectUtils.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{
	struct CSharpScriptResource
	{
		enum
		{
			Name,
		};
	};

	namespace
	{
		String ToIdentifier(StringView name)
		{
			String result;
			for (char c : name)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
				{
					result.Append(c);
				}
			}

			if (result.Empty() || (result[0] >= '0' && result[0] <= '9'))
			{
				result = "_" + result;
			}

			return result;
		}

		String BuildScriptContent(StringView className)
		{
			String content;
			content += "namespace ";
			content += ToIdentifier(Path::Name(Editor::GetProjectPath()));
			content += ";\n\npublic class ";
			content += ToIdentifier(className);
			content += "\n{\n    \n}\n";
			return content;
		}

		std::mutex dotnetBuildMutex;
		bool       dotnetBuildRunning = false;
		bool       dotnetBuildQueued = false;

		void RebuildDotnetProject()
		{
			String projectPath = Editor::GetProjectPath();
			if (!HasDotnetProject(projectPath))
			{
				return;
			}

			{
				std::scoped_lock lock(dotnetBuildMutex);
				if (dotnetBuildRunning)
				{
					dotnetBuildQueued = true;
					return;
				}
				dotnetBuildRunning = true;
			}

			Editor::AddTask([projectPath]
			{
				for (;;)
				{
					BuildDotnetProject(projectPath);

					std::scoped_lock lock(dotnetBuildMutex);
					if (!dotnetBuildQueued)
					{
						dotnetBuildRunning = false;
						return;
					}
					dotnetBuildQueued = false;
				}
			});
		}

		bool CanCreateCSharpScript(const MenuItemEventData& eventData)
		{
			return ProjectBrowserWindow::CanCreateAsset(eventData) && HasDotnetProject(Editor::GetProjectPath());
		}
	}

	struct CSharpScriptHandler : ResourceAssetHandler
	{
		SK_CLASS(CSharpScriptHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".cs";
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<CSharpScriptResource>::ID();
		}

		StringView GetDesc() override
		{
			return "C# Component";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_FILE_CODE;
		}

		void OpenAsset(RID asset) override
		{
			StringView absolutePath = ResourceAssets::GetAbsolutePath(asset);
			if (FileSystem::GetFileStatus(absolutePath).exists)
			{
				OpenDotnetFileInEditor(Editor::GetProjectPath(), absolutePath);
			}
			else
			{
				Editor::ShowErrorDialog("Please save your script file before opening it in an external tool.");
			}
		}

		RID Load(RID asset, StringView absolutePath) override
		{
			RID            resource = Resources::Create(GetResourceTypeId(), UUID::RandomUUID());
			ResourceObject object = Resources::Write(resource);
			object.SetString(CSharpScriptResource::Name, Path::Name(absolutePath));
			object.Commit();
			return resource;
		}

		void Reloaded(RID asset, StringView absolutePath) override
		{
			RebuildDotnetProject();
		}

		void Save(RID asset, StringView absolutePath) override
		{
			if (FileSystem::GetFileStatus(absolutePath).exists)
			{
				return;
			}

			FileSystem::SaveFileAsString(absolutePath, BuildScriptContent(Path::Name(absolutePath)));
		}

		void AfterMove(RID asset, StringView oldAbsolutePath, StringView newAbsolutePath) override
		{
			if (!FileSystem::GetFileStatus(newAbsolutePath).exists)
			{
				return;
			}

			String oldClass = ToIdentifier(Path::Name(oldAbsolutePath));
			String newClass = ToIdentifier(Path::Name(newAbsolutePath));
			if (oldClass == newClass)
			{
				return;
			}

			String content = FileSystem::ReadFileAsString(newAbsolutePath);
			String search = "class " + oldClass;

			const char* found = strstr(content.CStr(), search.CStr());
			if (found == nullptr)
			{
				return;
			}

			usize pos = found - content.CStr();

			String result;
			result.Append(content.CStr(), content.CStr() + pos);
			result += "class " + newClass;
			result.Append(content.CStr() + pos + search.Size(), content.CStr() + content.Size());

			FileSystem::SaveFileAsString(newAbsolutePath, result);
		}
	};

	void RegisterCSharpScriptHandler()
	{
		Resources::Type<CSharpScriptResource>()
			.Field<CSharpScriptResource::Name>(ResourceFieldType::String)
			.Build();

		Reflection::Type<CSharpScriptHandler>();

		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New C# Component",
			.icon = ICON_FA_FILE_CODE,
			.priority = 35,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = CanCreateCSharpScript,
			.userData = TypeInfo<CSharpScriptResource>::ID()
		});
	}
}
