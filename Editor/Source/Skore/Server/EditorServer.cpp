#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Skore/Server/EditorServer.hpp"

#include "Skore/Editor.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/Traits.hpp"
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
