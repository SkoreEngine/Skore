#include "Skore/Scene/SceneManager.hpp"

#include "Skore/Scene/Scene.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"


namespace Skore
{
	static Scene* activeScene = nullptr;


	void SceneManager::SetActiveScene(Scene* scene)
	{
		if (activeScene != scene)
		{
			if (activeScene)
			{
				activeScene->OnSceneDeactivated();
			}

			if (scene)
			{
				scene->OnSceneActivated();
			}
			activeScene = scene;
		}
	}

	Scene* SceneManager::GetActiveScene()
	{
		return activeScene;
	}

	void SceneManager::RegisterType(NativeReflectType<SceneManager>& type)
	{
		type.Function<&SceneManager::SetActiveScene>("SetActiveScene", "scene");
		type.Function<&SceneManager::GetActiveScene>("GetActiveScene");
		Event::Bind<OnUpdate, Update>();
	}

	void SceneManager::Update()
	{
		if (activeScene)
		{
			activeScene->Update();
		}
	}
}