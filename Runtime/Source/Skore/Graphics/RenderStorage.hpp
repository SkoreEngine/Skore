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
#include "Device.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/MeshRenderComponent.hpp"

namespace Skore
{
	class MeshAsset;
	class MaterialAsset;
	class LightComponent;

	struct MeshRenderData
	{
		MeshAsset*            mesh;
		Mat4                  transform;
		Array<MaterialAsset*> materials;
		bool                  visible = true;
		bool				  castShadows = false;
	};

	struct EnvironmentRenderData
	{
		MaterialAsset* skyboxMaterial;
		bool visible = true;
	};

	enum class LightTypeData
	{
		Directional,
		Point,
		Spot
	};

	struct LightRenderData
	{
		LightTypeData type = LightTypeData::Directional;
		Mat4          transform;
		Color         color = Color::WHITE;
		f32           intensity = 1.0f;
		f32           range = 10.0f;
		f32           innerConeAngle = Math::Radians(25.0f);
		f32           outerConeAngle = Math::Radians(30.0f);
		bool          visible = true;
		bool		  enableShadows = true;
	};

	class SK_API RenderStorage
	{
	public:
		virtual ~RenderStorage() = default;
		RenderStorage() = default;

		void RegisterMeshProxy(VoidPtr ptr);
		void RemoveMeshProxy(VoidPtr ptr);
		void SetMeshTransform(VoidPtr ptr, const Mat4& worldTransform);
		void SetMesh(VoidPtr ptr, MeshAsset* meshAsset);
		void SetMeshVisible(VoidPtr ptr, bool visible);
		void SetMeshMaterials(VoidPtr ptr, Span<MaterialAsset*> materials);
		void SetMeshCastShadows(MeshRenderComponent* meshRenderComponent, bool castShadows);

		void RegisterEnvironmentProxy(VoidPtr ptr);
		void RemoveEnvironmentProxy(VoidPtr ptr);
		void SetEnvironmentSkyboxMaterial(VoidPtr ptr, MaterialAsset* material);
		void SetEnvironmentVisible(VoidPtr ptr, bool visible);

		void RegisterLightProxy(VoidPtr ptr);
		void RemoveLightProxy(VoidPtr ptr);
		void SetLightTransform(VoidPtr ptr, const Mat4& worldTransform);
		void SetLightType(VoidPtr ptr, LightComponent::LightType type);
		void SetLightColor(VoidPtr ptr, const Color& color);
		void SetLightIntensity(VoidPtr ptr, f32 intensity);
		void SetLightRange(VoidPtr ptr, f32 range);
		void SetLightInnerConeAngle(VoidPtr ptr, f32 angle);
		void SetLightOuterConeAngle(VoidPtr ptr, f32 angle);
		void SetLightVisible(VoidPtr ptr, bool visible);
		void SetLightEnableShadows(LightComponent* lightComponent, bool enableShadows);

		ankerl::unordered_dense::map<u64, MeshRenderData> meshes;
		ankerl::unordered_dense::map<u64, EnvironmentRenderData> environments;
		ankerl::unordered_dense::map<u64, LightRenderData> lights;
	};
}
