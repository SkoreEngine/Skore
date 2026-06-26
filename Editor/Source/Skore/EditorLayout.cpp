#include "Skore/EditorLayout.hpp"

#include <imgui.h>

#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore::EditorLayout
{
	namespace
	{
		struct WorkspaceLayout
		{
			u8                       workspaceTypeId{};
			Array<SavedEditorWindow> openWindows;
		};

		Logger& logger = Logger::GetLogger("Skore::EditorLayout");

		String                 filePath;
		Array<WorkspaceLayout> workspaceLayouts;
		String                 imguiSettings;
		String                 lastSerializedImGuiSettings;
		bool                   dirty = false;

		FnCaptureWindowStates  captureCallback = nullptr;
		VoidPtr                captureUserData = nullptr;

		//pull fresh per-window state from the owner (see SetCaptureCallback / SaveWindowState)
		void CaptureWindowStates()
		{
			if (captureCallback)
			{
				captureCallback(captureUserData);
			}
		}

		WorkspaceLayout* FindWorkspace(u8 workspaceTypeId)
		{
			for (WorkspaceLayout& wl : workspaceLayouts)
			{
				if (wl.workspaceTypeId == workspaceTypeId) return &wl;
			}
			return nullptr;
		}

		WorkspaceLayout& GetOrCreateWorkspace(u8 workspaceTypeId)
		{
			if (WorkspaceLayout* found = FindWorkspace(workspaceTypeId)) return *found;
			workspaceLayouts.EmplaceBack(WorkspaceLayout{workspaceTypeId, {}});
			return workspaceLayouts.Back();
		}

		void CaptureImGuiSettings()
		{
			usize       size = 0;
			const char* data = ImGui::SaveIniSettingsToMemory(&size);
			if (data && size > 0)
			{
				imguiSettings = StringView(data, size);
			}
			else
			{
				imguiSettings.Clear();
			}
		}

		void WriteToDisk()
		{
			CaptureWindowStates();
			CaptureImGuiSettings();

			JsonArchiveWriter writer;
			writer.WriteInt("version", 1);
			writer.WriteString("imguiSettings", imguiSettings);

			writer.BeginSeq("workspaces");
			for (const WorkspaceLayout& wl : workspaceLayouts)
			{
				if (wl.workspaceTypeId != WorkspaceTypes::Scene) continue;

				writer.BeginMap();
				writer.WriteUInt("type", wl.workspaceTypeId);
				writer.BeginSeq("windows");
				for (const SavedEditorWindow& w : wl.openWindows)
				{
					ReflectType* rt = Reflection::FindTypeById(w.typeId);
					if (!rt) continue;

					writer.BeginMap();
					writer.WriteString("type", rt->GetName());
					writer.WriteUInt("id", w.id);
					if (!w.state.Empty())
					{
						writer.WriteString("state", w.state);
					}
					writer.EndMap();
				}
				writer.EndSeq();
				writer.EndMap();
			}
			writer.EndSeq();

			String parent = Path::Parent(filePath);
			if (!FileSystem::GetFileStatus(parent).exists)
			{
				FileSystem::CreateDirectory(parent);
			}

			FileSystem::SaveFileAsString(filePath, writer.EmitAsString());
			lastSerializedImGuiSettings = imguiSettings;
			dirty = false;
		}
	}

	void Init()
	{
		filePath = Path::Join(FileSystem::AppFolder(), "Skore", "EditorLayout.json");

		if (ImGui::GetCurrentContext())
		{
			ImGui::GetIO().IniSavingRate = 0.5f;
		}

		if (!FileSystem::GetFileStatus(filePath).exists)
		{
			return;
		}

		String content = FileSystem::ReadFileAsString(filePath);
		if (content.Empty())
		{
			return;
		}

		JsonArchiveReader reader(content);

		imguiSettings = reader.ReadString("imguiSettings");

		if (reader.BeginSeq("workspaces"))
		{
			while (reader.NextSeqEntry())
			{
				reader.BeginMap();
				u8               workspaceTypeId = static_cast<u8>(reader.ReadUInt("type"));
				WorkspaceLayout& wl = GetOrCreateWorkspace(workspaceTypeId);

				if (reader.BeginSeq("windows"))
				{
					while (reader.NextSeqEntry())
					{
						reader.BeginMap();
						StringView   typeName = reader.ReadString("type");
						u32          id = static_cast<u32>(reader.ReadUInt("id"));
						StringView   state = reader.ReadString("state");
						ReflectType* rt = Reflection::FindTypeByName(typeName);
						if (rt)
						{
							wl.openWindows.EmplaceBack(SavedEditorWindow{rt->GetProps().typeId, id, state});
						}
						else
						{
							logger.Debug("Skipping unknown window type '{}' in saved layout", typeName);
						}
						reader.EndMap();
					}
					reader.EndSeq();
				}

				reader.EndMap();
			}
			reader.EndSeq();
		}

		if (!imguiSettings.Empty())
		{
			ImGui::LoadIniSettingsFromMemory(imguiSettings.CStr(), imguiSettings.Size());
			lastSerializedImGuiSettings = imguiSettings;
		}
	}

	void Shutdown()
	{
		//capture before the dirty check so window-only state changes still force a final write
		CaptureWindowStates();

		if (ImGui::GetCurrentContext())
		{
			CaptureImGuiSettings();
			if (imguiSettings != lastSerializedImGuiSettings)
			{
				dirty = true;
			}
			ImGui::GetIO().WantSaveIniSettings = false;
		}

		if (dirty && !filePath.Empty())
		{
			WriteToDisk();
		}

		workspaceLayouts.Clear();
		imguiSettings.Clear();
		lastSerializedImGuiSettings.Clear();
		filePath.Clear();
		dirty = false;
		captureCallback = nullptr;
		captureUserData = nullptr;
	}

	bool HasSavedWorkspace(u8 workspaceTypeId)
	{
		return FindWorkspace(workspaceTypeId) != nullptr;
	}

	Span<SavedEditorWindow> GetSavedWindows(u8 workspaceTypeId)
	{
		if (WorkspaceLayout* wl = FindWorkspace(workspaceTypeId))
		{
			return wl->openWindows;
		}
		return {};
	}

	void OnWindowOpened(u8 workspaceTypeId, TypeID windowType, u32 id)
	{
		WorkspaceLayout& wl = GetOrCreateWorkspace(workspaceTypeId);
		for (const SavedEditorWindow& w : wl.openWindows)
		{
			if (w.id == id) return;
		}
		wl.openWindows.EmplaceBack(SavedEditorWindow{windowType, id});
		dirty = true;
	}

	void OnWindowClosed(u8 workspaceTypeId, u32 id)
	{
		WorkspaceLayout* wl = FindWorkspace(workspaceTypeId);
		if (!wl) return;

		for (usize i = 0; i < wl->openWindows.Size(); ++i)
		{
			if (wl->openWindows[i].id == id)
			{
				wl->openWindows.Erase(wl->openWindows.begin() + i, wl->openWindows.begin() + i + 1);
				dirty = true;
				return;
			}
		}
	}

	void SetCaptureCallback(FnCaptureWindowStates callback, VoidPtr userData)
	{
		captureCallback = callback;
		captureUserData = userData;
	}

	void SaveWindowState(u8 workspaceTypeId, u32 id, StringView state)
	{
		WorkspaceLayout* wl = FindWorkspace(workspaceTypeId);
		if (!wl) return;

		for (SavedEditorWindow& w : wl->openWindows)
		{
			if (w.id == id)
			{
				if (w.state != state)
				{
					w.state = state;
					dirty = true;
				}
				return;
			}
		}
	}

	void Flush()
	{
		if (ImGui::GetCurrentContext() && ImGui::GetIO().WantSaveIniSettings)
		{
			CaptureImGuiSettings();
			ImGui::GetIO().WantSaveIniSettings = false;
			if (imguiSettings != lastSerializedImGuiSettings)
			{
				dirty = true;
			}
		}

		if (!dirty || filePath.Empty()) return;
		WriteToDisk();
	}

	void ResetAll()
	{
		workspaceLayouts.Clear();
		imguiSettings.Clear();
		lastSerializedImGuiSettings.Clear();
		if (!filePath.Empty())
		{
			FileSystem::Remove(filePath, false);
		}
		if (ImGui::GetCurrentContext())
		{
			ImGui::LoadIniSettingsFromMemory("", 0);
			ImGui::GetIO().WantSaveIniSettings = false;
		}
		dirty = false;
	}
}
