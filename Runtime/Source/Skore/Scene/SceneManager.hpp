#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
	class Scene;

	class SK_API SceneManager
	{
	public:
		static void   SetActiveScene(Scene* scene);
		static Scene* GetActiveScene();

		static void RegisterType(NativeReflectType<SceneManager>& type);

	private:
		static void Update();
	};
}