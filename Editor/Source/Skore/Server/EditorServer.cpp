#include <httplib.h>
#include <yyjson.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Skore/Server/EditorServer.hpp"

#include "Skore/Editor.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/ResourceType.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	void RegisterAssetApiRoutes(httplib::Server& svr);

	namespace
	{
		constexpr u16        defaultPort = 8090;
		constexpr StringView defaultHost = "127.0.0.1";

		Logger& logger = Logger::GetLogger("Skore::EditorServer");

		httplib::Server* server = nullptr;
		std::thread      serverThread;

		std::atomic_bool running{false};
		std::atomic_bool shuttingDown{false};

		String currentHost;
		u16    currentPort = 0;

		bool   appliedEnabled = false;
		u16    appliedPort = 0;
		String appliedHost;

		std::chrono::steady_clock::time_point lastSettingsCheck{};

		bool mcpInstalledCache = false;

		bool RunOnMainThreadSync(std::function<void()> fn, u32 timeoutMs = 3000)
		{
			if (shuttingDown.load())
			{
				return false;
			}

			struct SyncCall
			{
				std::mutex              mutex;
				std::condition_variable cv;
				bool                    done = false;
			};

			auto call = std::make_shared<SyncCall>();

			Editor::ExecuteOnMainThread([call, fn = Traits::Move(fn)]()
			{
				fn();
				{
					std::scoped_lock lock(call->mutex);
					call->done = true;
				}
				call->cv.notify_all();
			});

			std::unique_lock lock(call->mutex);
			call->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]
			{
				return call->done || shuttingDown.load();
			});
			return call->done;
		}

		void WriteJson(httplib::Response& res, int status, StringView json)
		{
			res.status = status;
			res.set_content(json.CStr(), json.Size(), "application/json");
		}

		void SetupRoutes(httplib::Server& svr)
		{
			svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res)
			{
				res.set_header("Access-Control-Allow-Origin", "*");
				res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
				res.set_header("Access-Control-Allow-Headers", "Content-Type");

				if (req.method == "OPTIONS")
				{
					res.status = 204;
					return httplib::Server::HandlerResponse::Handled;
				}
				return httplib::Server::HandlerResponse::Unhandled;
			});

			svr.Get("/", [](const httplib::Request&, httplib::Response& res)
			{
				res.set_content("Skore Editor HTTP Server", "text/plain");
			});

			svr.Get("/api/ping", [](const httplib::Request&, httplib::Response& res)
			{
				JsonArchiveWriter writer;
				writer.WriteString("engine", "Skore");
				writer.WriteString("status", "ok");
				WriteJson(res, 200, writer.EmitAsString());
			});

			svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res)
			{
				String projectPath;
				u32    workspaceCount = 0;

				if (!RunOnMainThreadSync([&]
				{
					projectPath = Editor::GetProjectPath();
					workspaceCount = Editor::GetWorkspaceCount();
				}))
				{
					JsonArchiveWriter writer;
					writer.WriteString("error", "editor is busy or shutting down");
					WriteJson(res, 503, writer.EmitAsString());
					return;
				}

				JsonArchiveWriter writer;
				writer.WriteString("engine", "Skore");
				writer.WriteString("projectPath", projectPath);
				writer.WriteUInt("workspaceCount", workspaceCount);
				WriteJson(res, 200, writer.EmitAsString());
			});

			svr.set_error_handler([](const httplib::Request&, httplib::Response& res)
			{
				if (res.body.empty())
				{
					JsonArchiveWriter writer;
					writer.WriteString("error", "not found");
					writer.WriteInt("status", res.status);
					String json = writer.EmitAsString();
					res.set_content(json.CStr(), json.Size(), "application/json");
				}
			});

			svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep)
			{
				String message = "internal error";
				try
				{
					if (ep) std::rethrow_exception(ep);
				}
				catch (const std::exception& e)
				{
					message = e.what();
				}
				catch (...) {}

				JsonArchiveWriter writer;
				writer.WriteString("error", message);
				WriteJson(res, 500, writer.EmitAsString());
			});

			RegisterAssetApiRoutes(svr);
		}

		void StopServer()
		{
			if (server)
			{
				server->stop();
			}
			if (serverThread.joinable())
			{
				serverThread.join();
			}
			delete server;
			server = nullptr;
			running = false;
		}

		void StartServer(const String& host, u16 port)
		{
			StopServer();

			server = new httplib::Server();
			SetupRoutes(*server);

			std::string boundHost = std::string(host.CStr());
			if (!server->bind_to_port(boundHost, port))
			{
				logger.Error("Failed to bind http server to {}:{} (port may be in use)", host, port);
				delete server;
				server = nullptr;
				return;
			}

			running = true;
			currentHost = host;
			currentPort = port;

			httplib::Server* boundServer = server;
			serverThread = std::thread([boundServer, boundHost, port]()
			{
				logger.Info("Http server listening on {}:{}", boundHost.c_str(), port);
				boundServer->listen_after_bind();
				logger.Info("Http server stopped");
			});
		}

		bool ReadSettings(bool& enabled, u16& port, String& host)
		{
			RID rid = Settings::Get<EditorSettings, HttpServerSettings>();
			if (!rid)
			{
				return false;
			}

			if (ResourceObject obj = Resources::Read(rid))
			{
				enabled = obj.GetBool(HttpServerSettings::Enabled);
				port = static_cast<u16>(obj.GetUInt(HttpServerSettings::Port));
				host = obj.GetString(HttpServerSettings::Host);

				if (port == 0) port = defaultPort;
				if (host.Empty()) host = defaultHost;
				return true;
			}
			return false;
		}

		void ApplySettings()
		{
			bool   enabled = false;
			u16    port = defaultPort;
			String host;

			if (!ReadSettings(enabled, port, host))
			{
				return;
			}

			if (enabled == appliedEnabled && port == appliedPort && host == appliedHost)
			{
				return;
			}

			appliedEnabled = enabled;
			appliedPort = port;
			appliedHost = host;

			if (!enabled)
			{
				if (running)
				{
					logger.Info("Http server disabled via settings");
					StopServer();
				}
				return;
			}

			StartServer(host, port);
		}

		String McpBundlePath()
		{
			String exe = Platform::GetExecutablePath();
			if (exe.Empty()) return {};
			return Path::Join(Path::Parent(exe), "Mcp", "skore-mcp.mjs");
		}

		String McpConfigPath()
		{
			StringView project = Editor::GetProjectPath();
			if (project.Empty()) return {};
			return Path::Join(project, ".mcp.json");
		}

		String McpServerUrl()
		{
			bool   enabled = false;
			u16    port = defaultPort;
			String host;
			if (!ReadSettings(enabled, port, host))
			{
				port = defaultPort;
				host = defaultHost;
			}
			return String("http://").Append(host).Append(":").Append(ToString(static_cast<u64>(port)));
		}

		String McpConfigJson(StringView bundlePath, StringView url)
		{
			JsonArchiveWriter writer;
			writer.BeginMap("mcpServers");
			writer.BeginMap("skore");
			writer.WriteString("command", "node");
			writer.BeginSeq("args");
			writer.AddString(bundlePath);
			writer.EndSeq();
			writer.BeginMap("env");
			writer.WriteString("SKORE_MCP_URL", url);
			writer.EndMap();
			writer.EndMap();
			writer.EndMap();
			return writer.EmitAsString();
		}

		void JsonMergeObjInto(yyjson_mut_doc* doc, yyjson_mut_val* target, yyjson_val* patch)
		{
			yyjson_obj_iter iter;
			yyjson_obj_iter_init(patch, &iter);
			while (yyjson_val* key = yyjson_obj_iter_next(&iter))
			{
				yyjson_val* patchValue = yyjson_obj_iter_get_val(key);
				const char* keyStr = yyjson_get_str(key);
				size_t      keyLen = yyjson_get_len(key);

				yyjson_mut_val* existing = yyjson_mut_obj_getn(target, keyStr, keyLen);
				if (existing && yyjson_mut_is_obj(existing) && yyjson_is_obj(patchValue))
				{
					JsonMergeObjInto(doc, existing, patchValue);
				}
				else
				{
					yyjson_mut_val* mutKey = yyjson_mut_strncpy(doc, keyStr, keyLen);
					yyjson_mut_val* mutValue = yyjson_val_mut_copy(doc, patchValue);
					yyjson_mut_obj_put(target, mutKey, mutValue);
				}
			}
		}

		//deep-merges `patch` (our {mcpServers:{skore:...}}) into `base`, preserving any other
		//servers/keys already present, and returns the merged pretty JSON.
		String MergeMcpJson(StringView base, StringView patch)
		{
			yyjson_doc* patchDoc = yyjson_read(patch.Data(), patch.Size(), 0);
			if (!patchDoc)
			{
				return String(base);
			}

			yyjson_doc*     baseDoc = base.Empty() ? nullptr : yyjson_read(base.Data(), base.Size(), 0);
			yyjson_val*     patchRoot = yyjson_doc_get_root(patchDoc);
			yyjson_mut_doc* out = nullptr;

			if (baseDoc && yyjson_is_obj(yyjson_doc_get_root(baseDoc)))
			{
				out = yyjson_doc_mut_copy(baseDoc, nullptr);
				if (out && yyjson_is_obj(patchRoot))
				{
					JsonMergeObjInto(out, yyjson_mut_doc_get_root(out), patchRoot);
				}
			}

			if (!out)
			{
				out = yyjson_mut_doc_new(nullptr);
				yyjson_mut_doc_set_root(out, yyjson_val_mut_copy(out, patchRoot));
			}

			size_t len = 0;
			char*  result = yyjson_mut_write_opts(out, YYJSON_WRITE_PRETTY, nullptr, &len, nullptr);
			String output = result ? String(result, len) : String(patch);
			if (result) free(result);

			yyjson_mut_doc_free(out);
			if (baseDoc) yyjson_doc_free(baseDoc);
			yyjson_doc_free(patchDoc);
			return output;
		}

		bool ComputeMcpInstalled()
		{
			String path = McpConfigPath();
			if (path.Empty() || !FileSystem::GetFileStatus(path).exists) return false;

			JsonArchiveReader reader(FileSystem::ReadFileAsString(path));
			bool              found = false;
			if (reader.BeginMap("mcpServers"))
			{
				while (reader.NextMapEntry())
				{
					if (reader.GetCurrentKey() == "skore")
					{
						found = true;
						break;
					}
				}
				reader.EndMap();
			}
			return found;
		}

		void RefreshMcpInstalled()
		{
			mcpInstalledCache = ComputeMcpInstalled();
		}
	}

	bool ServerRunOnMainThreadSync(std::function<void()> fn, u32 timeoutMs)
	{
		return RunOnMainThreadSync(Traits::Move(fn), timeoutMs);
	}

	void EditorServer::Init()
	{
		shuttingDown = false;
		appliedEnabled = false;
		appliedPort = 0;
		appliedHost.Clear();
		lastSettingsCheck = std::chrono::steady_clock::now();
		ApplySettings();
		RefreshMcpInstalled();
	}

	void EditorServer::Update()
	{
		constexpr auto interval = std::chrono::milliseconds(500);
		const auto     now = std::chrono::steady_clock::now();
		if (now - lastSettingsCheck < interval)
		{
			return;
		}
		lastSettingsCheck = now;

		ApplySettings();
	}

	void EditorServer::Shutdown()
	{
		shuttingDown = true;
		StopServer();
	}

	bool EditorServer::IsRunning()
	{
		return running.load();
	}

	u16 EditorServer::GetPort()
	{
		return currentPort;
	}

	StringView EditorServer::GetHost()
	{
		return currentHost;
	}

	bool EditorServer::IsMcpInstalled()
	{
		return mcpInstalledCache;
	}

	void EditorServer::InstallMcp()
	{
		String bundle = McpBundlePath();
		if (bundle.Empty() || !FileSystem::GetFileStatus(bundle).exists)
		{
			Editor::ShowErrorDialog("Skore MCP bundle not found.\nBuild the project first - it deploys to <bin>/Mcp/skore-mcp.mjs.");
			return;
		}

		String configPath = McpConfigPath();
		if (configPath.Empty())
		{
			Editor::ShowErrorDialog("No project is open.");
			return;
		}

		if (RID settings = Settings::Get<EditorSettings, HttpServerSettings>())
		{
			ResourceObject obj = Resources::Write(settings);
			obj.SetBool(HttpServerSettings::Enabled, true);
			obj.Commit();
		}

		String url = McpServerUrl();
		String json = McpConfigJson(bundle, url);

		String contents = FileSystem::GetFileStatus(configPath).exists
			? MergeMcpJson(FileSystem::ReadFileAsString(configPath), json)
			: json;
		FileSystem::SaveFileAsString(configPath, contents);

		Platform::SetClipboardText(json);
		RefreshMcpInstalled();

		String message = String("Skore MCP installed.\n\n")
			.Append("- Http Server enabled (").Append(url).Append(")\n")
			.Append("- Updated ").Append(configPath).Append("\n\n")
			.Append("Claude Code will detect it in this project (approve it when prompted).\n")
			.Append("The 'skore' config was also copied to your clipboard for other clients (Cursor / Claude Desktop).");
		Editor::ShowConfirmDialog(message, nullptr, nullptr);
	}

	void RegisterEditorServerTypes()
	{
		ResourceType* type = Resources::Type<HttpServerSettings>()
			.Field<HttpServerSettings::Enabled>(ResourceFieldType::Bool)
			.Field<HttpServerSettings::Port>(ResourceFieldType::UInt)
			.Field<HttpServerSettings::Host>(ResourceFieldType::String)
			.Attribute<EditableSettings>(EditableSettings{
				.path = "Editor/Http Server",
				.type = TypeInfo<EditorSettings>::ID(),
				.order = 10
			})
			.Build()
			.GetResourceType();

		RID rid = Resources::Create<HttpServerSettings>();
		ResourceObject settings = Resources::Write(rid);
		settings.SetBool(HttpServerSettings::Enabled, false);
		settings.SetUInt(HttpServerSettings::Port, defaultPort);
		settings.SetString(HttpServerSettings::Host, defaultHost);
		settings.Commit();
		type->SetDefaultValue(rid);
	}
}
