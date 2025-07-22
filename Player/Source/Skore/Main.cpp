// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Skore/App.hpp"
#include "Skore/Main.hpp"

#include "Skore/Events.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Graphics/BasicSceneRenderer.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneManager.hpp"

namespace Skore
{
	StdOutSink stdOutSink{};
	SceneRendererViewport* sceneRendererViewport = nullptr;

	Extent currentExtent;

	void RecordRenderCommands(GPUCommandBuffer* commandBuffer)
	{
		if (currentExtent != Graphics::GetSwapchainExtent())
		{
			currentExtent = Graphics::GetSwapchainExtent();
			sceneRendererViewport->Resize(currentExtent);
		}

		Scene* scene = SceneManager::GetActiveScene();
		RenderStorage* storage = scene ? scene->GetRenderStorage() : nullptr;

		//TODO: camera code needs to be improved
		if (storage)
		{
			if (std::optional<CameraRenderData> camera = storage->GetCurrentCamera())
			{
				Mat4 currentProjection = {};
				if (camera->projection == Camera::Projection::Perspective)
				{
					f32 aspectRatio = static_cast<f32>(currentExtent.width) / static_cast<f32>(currentExtent.height);
					currentProjection = Math::Perspective(Math::Radians(camera->fov), aspectRatio, camera->nearPlane, camera->farPlane);
				}
				else
				{
					//get current values here?
					currentProjection = Math::Ortho(0, 0, 10, 10, camera->nearPlane, camera->farPlane);
				}
				sceneRendererViewport->SetCamera(camera->nearPlane, camera->farPlane, camera->viewMatrix, currentProjection, camera->position);
			}
		}
		sceneRendererViewport->Render(storage, commandBuffer);
	}


	void SwapchainBlit(GPUCommandBuffer* cmd, GPURenderPass* swapchainRenderPass)
	{
		sceneRendererViewport->Blit(swapchainRenderPass, cmd);
	}

	void AppShutdown()
	{
		DestroyAndFree(sceneRendererViewport);
	}

	i32 Main(int argc, char** argv)
	{
		Logger::RegisterSink(stdOutSink);

		App::TypeRegister();

		AppConfig appConfig;
		appConfig.fullscreen = false;
		appConfig.width = 1920;
		appConfig.height = 1080;
		appConfig.title = "Skore Player";

		if (App::Init(appConfig, argc, argv) != AppResult::Continue)
		{
			return 1;
		}

		ArgParser& args = App::GetArgs();
		String currentDir = args.Get("current-path");
		currentDir = !currentDir.Empty() ? currentDir : FileSystem::CurrentDir();

		//------------------- step 1 -- Load Plugins
		for (const String& file : DirectoryEntries(Path::Join(currentDir, "Plugins")))
		{
			App::LoadPlugin(file);
		}

		const String configFile = Path::Join(currentDir, "Engine.bcfg");
		{
			Array<u8> buffer;
			FileSystem::ReadFileAsByteArray(configFile, buffer);
			BinaryArchiveReader reader{buffer};

			reader.BeginMap("projectSettings");
			Settings::Load(reader, TypeInfo<ProjectSettings>::ID());
			reader.EndMap();
		}

		bool resourceLoaded = false;

		//------------------- step 2 -- Resource Loading
		for (const String& file : DirectoryEntries(currentDir))
		{
			if (Path::Extension(file) == ".pak")
			{
				resourceLoaded = true;
				Resources::LoadPackage(file);
			}
		}

		if (resourceLoaded)
		{
			//---------------- step 3 - setup render------------
			sceneRendererViewport = Alloc<SceneRendererViewport>();
			sceneRendererViewport->Init();

			Event::Bind<OnSwapchainBlit, SwapchainBlit>();
			Event::Bind<OnRecordRenderCommands, RecordRenderCommands>();
			Event::Bind<OnShutdown, AppShutdown>();

			//------------step3 - Main scene load------------

			RID sceneSettings = Settings::Get<ProjectSettings, SceneSettings>();
			if (ResourceObject sceneSettingsObject = Resources::Read(sceneSettings))
			{
				if (RID defaultScene = sceneSettingsObject.GetReference(SceneSettings::DefaultScene))
				{
					Scene* scene = Alloc<Scene>(defaultScene);
					SceneManager::SetActiveScene(scene);
				}
			}

		}

		App::Run();

		return 0;
	}
}
