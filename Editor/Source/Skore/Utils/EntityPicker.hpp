#pragma once
#include "Skore/Core/Array.hpp"
#include "Skore/Graphics/Device.hpp"

namespace Skore
{
	class Entity;
	class SceneEditor;
	class Scene;

	class SK_API EntityPicker
	{
	public:
		EntityPicker() = default;
		~EntityPicker();

		void    Resize(Extent extent);
		Entity* PickEntity(Mat4 viewProjection, SceneEditor* sceneEditor, Vec2 mousePosition);

	private:
		Extent              currentExtent = {};
		GPUTexture*         texture = nullptr;
		GPUTexture*         depth = nullptr;
		GPUBuffer*          imageBuffer = nullptr;
		GPURenderPass*      renderPass = nullptr;
		GPUFramebuffer*     framebuffer = nullptr;
		Array<GPUPipeline*> pickerPipelines;
		Scene*              cachedPipelineOwner = nullptr;

		void CleanupPipelines();
		void DestroyObjects();
	};
}
