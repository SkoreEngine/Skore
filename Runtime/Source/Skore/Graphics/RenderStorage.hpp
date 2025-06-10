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

namespace Skore
{
	class MeshAsset;
	class MaterialAsset;

	struct MeshRenderData
	{
		MeshAsset*            mesh;
		Mat4                  transform;
		Array<MaterialAsset*> materials;
		bool                  visible = true;
		bool                  castShadows = false;
	};

	struct EnvironmentRenderData
	{
		MaterialAsset* skyboxMaterial;
		bool           visible = true;
	};

	enum class RendererLightType
	{
		Directional,
		Point,
		Spot
	};

	struct LightRenderData
	{
		RendererLightType type = RendererLightType::Directional;
		Mat4              transform;
		Color             color = Color::WHITE;
		f32               intensity = 1.0f;
		f32               range = 10.0f;
		f32               innerConeAngle = Math::Radians(25.0f);
		f32               outerConeAngle = Math::Radians(30.0f);
		bool              visible = true;
		bool              enableShadows = true;
	};

	class SK_API RenderStorage
	{
	public:
		virtual ~RenderStorage() = default;
		RenderStorage() = default;

		void RegisterMeshProxy(RID rid);
		void RemoveMeshProxy(RID rid);
		void SetMeshTransform(RID rid, const Mat4& worldTransform);
		void SetMesh(RID rid, MeshAsset* meshAsset);
		void SetMeshVisible(RID rid, bool visible);
		void SetMeshMaterials(RID rid, Span<MaterialAsset*> materials);
		void SetMeshCastShadows(RID rid, bool castShadows);

		void RegisterEnvironmentProxy(RID rid);
		void RemoveEnvironmentProxy(RID rid);
		void SetEnvironmentSkyboxMaterial(RID rid, MaterialAsset* material);
		void SetEnvironmentVisible(RID rid, bool visible);

		void RegisterLightProxy(RID rid);
		void RemoveLightProxy(RID rid);
		void SetLightTransform(RID rid, const Mat4& worldTransform);
		void SetLightType(RID rid, RendererLightType type);
		void SetLightColor(RID rid, const Color& color);
		void SetLightIntensity(RID rid, f32 intensity);
		void SetLightRange(RID rid, f32 range);
		void SetLightInnerConeAngle(RID rid, f32 angle);
		void SetLightOuterConeAngle(RID rid, f32 angle);
		void SetLightVisible(RID rid, bool visible);
		void SetLightEnableShadows(RID rid, bool enableShadows);

		ankerl::unordered_dense::map<RID, MeshRenderData>        meshes;
		ankerl::unordered_dense::map<RID, EnvironmentRenderData> environments;
		ankerl::unordered_dense::map<RID, LightRenderData>       lights;
	};
}
