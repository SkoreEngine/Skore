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

#pragma once

#include "Skore/Common.hpp"
#include "Device.hpp"
#include "GraphicsCommon.hpp"
#include "GraphicsResources.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Scene/Components/Camera.hpp"

namespace Skore
{
	struct TextureStorageData
	{
		GPUTexture* texture;
	};

	struct MaterialStorageData
	{
		MaterialResource::MaterialType type;
		GPUBuffer*                     materialBuffer;
		GPUDescriptorSet*              descriptorSet;
		TextureStorageData*            skyMaterialTexture;
	};

	struct MeshStorageData
	{
		GPUBuffer* vertexBuffer;
		GPUBuffer* indexBuffer;

		Array<MeshPrimitive>     primitives;
		Array<GPUDescriptorSet*> materials;
	};


	struct SK_API MeshRenderData
	{
		u64                      id;
		MeshStorageData*         mesh;
		Array<GPUDescriptorSet*> overrideMaterials;

		Mat4 transform;
		bool visible = true;
		bool castShadows = false;

		GPUBuffer*        bonesBuffer = nullptr;
		GPUDescriptorSet* bonesDescriptorSet = nullptr;

		GPUDescriptorSet* GetMaterial(u32 index) const;
	};

	struct EnvironmentRenderData
	{
		MaterialStorageData* skyboxMaterial;
		bool                 visible = true;
	};


	struct LightRenderData
	{
		u64       id;
		LightType type = LightType::Directional;
		Mat4      transform;
		Color     color = Color::WHITE;
		f32       intensity = 1.0f;
		f32       range = 10.0f;
		f32       innerConeAngle = Math::Radians(25.0f);
		f32       outerConeAngle = Math::Radians(30.0f);
		bool      visible = true;
		bool      enableShadows = true;
	};

	struct BonesRenderData
	{
		Mat4 boneMatrices[128];
	};


	struct CameraRenderData
	{
		u64  id;
		Mat4 viewMatrix;
		Vec3 position;
		f32  fov;
		f32  nearPlane;
		f32  farPlane;

		Camera::Projection projection;

		bool visible = true;
	};

	class SK_API RenderStorage
	{
	public:
		virtual ~RenderStorage() = default;
		RenderStorage() = default;

		void RegisterStaticMeshProxy(VoidPtr owner, u64 id);
		void RemoveStaticMeshProxy(VoidPtr owner);
		void SetStaticMeshTransform(VoidPtr owner, const Mat4& worldTransform);
		void SetStaticMesh(VoidPtr owner, RID meshAsset);
		void SetStaticMeshVisible(VoidPtr owner, bool visible);
		void SetStaticMeshMaterials(VoidPtr owner, Span<RID> materials);
		void SetStaticMeshCastShadows(VoidPtr owner, bool castShadows);

		void RegisterSkinnedMeshProxy(VoidPtr owner);
		void RemoveSkinnedMeshProxy(VoidPtr owner);
		void SetSkinnedMeshTransform(VoidPtr owner, const Mat4& worldTransform);
		void SetSkinnedMesh(VoidPtr owner, RID meshAsset);
		void SetSkinnedMeshVisible(VoidPtr owner, bool visible);
		void SetSkinnedMeshMaterials(VoidPtr owner, Span<RID> materials);
		void SetSkinnedMeshCastShadows(VoidPtr owner, bool castShadows);

		void RegisterEnvironmentProxy(VoidPtr owner);
		void RemoveEnvironmentProxy(VoidPtr owner);
		void SetEnvironmentSkyboxMaterial(VoidPtr owner, RID material);
		void SetEnvironmentVisible(VoidPtr owner, bool visible);

		void RegisterLightProxy(VoidPtr owner, u64 id);
		void RemoveLightProxy(VoidPtr owner);
		void SetLightTransform(VoidPtr owner, const Mat4& worldTransform);
		void SetLightType(VoidPtr owner, LightType type);
		void SetLightColor(VoidPtr owner, const Color& color);
		void SetLightIntensity(VoidPtr owner, f32 intensity);
		void SetLightRange(VoidPtr owner, f32 range);
		void SetLightInnerConeAngle(VoidPtr owner, f32 angle);
		void SetLightOuterConeAngle(VoidPtr owner, f32 angle);
		void SetLightVisible(VoidPtr owner, bool visible);
		void SetLightEnableShadows(VoidPtr owner, bool enableShadows);

		void RegisterCamera(VoidPtr owner, u64 id);
		void SetCameraViewMatrix(VoidPtr owner, const Mat4& viewMatrix);
		void SetCameraPosition(VoidPtr owner, const Vec3& position);
		void SetCameraProjection(VoidPtr owner, Camera::Projection projection);
		void SetCameraFov(VoidPtr owner, f32 fov);
		void SetCameraNear(VoidPtr owner, f32 near);
		void SetCameraFar(VoidPtr owner, f32 far);
		void SetCameraVisible(VoidPtr owner, bool visible);
		void RemoveCamera(VoidPtr owner);

		std::optional<CameraRenderData> GetCurrentCamera();

		ankerl::unordered_dense::map<VoidPtr, MeshRenderData>        staticMeshes;
		ankerl::unordered_dense::map<VoidPtr, MeshRenderData>        skinnedMeshes;
		ankerl::unordered_dense::map<VoidPtr, EnvironmentRenderData> environments;
		ankerl::unordered_dense::map<VoidPtr, LightRenderData>       lights;
		ankerl::unordered_dense::map<VoidPtr, CameraRenderData>      cameras;


	};
}
