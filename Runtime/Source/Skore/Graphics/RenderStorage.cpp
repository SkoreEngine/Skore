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

#include "RenderStorage.hpp"

namespace Skore
{
	void RenderStorage::RegisterMeshProxy(RID rid)
	{
		meshes.emplace(rid, MeshRenderData{
			               .mesh = {},
			               .transform = {},
			               .materials = {},
			               .visible = true,
		               });
	}


	void RenderStorage::RemoveMeshProxy(RID rid)
	{
		meshes.erase(rid);
	}

	void RenderStorage::SetMeshTransform(RID rid, const Mat4& worldTransform)
	{
		if (const auto& it = meshes.find(rid); it != meshes.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetMesh(RID rid, RID meshAsset)
	{
		if (const auto& it = meshes.find(rid); it != meshes.end())
		{
			it->second.mesh = meshAsset;
		}
	}

	void RenderStorage::SetMeshVisible(RID rid, bool visible)
	{
		if (const auto& it = meshes.find(rid); it != meshes.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetMeshMaterials(RID rid, Span<RID> materials)
	{
		if (const auto& it = meshes.find(rid); it != meshes.end())
		{
			it->second.materials = materials;
		}
	}

	void RenderStorage::SetMeshCastShadows(RID rid, bool castShadows)
	{
		if (const auto& it = meshes.find(rid); it != meshes.end())
		{
			it->second.castShadows = castShadows;
		}
	}

	void RenderStorage::RegisterEnvironmentProxy(RID rid)
	{
		environments.emplace(rid, EnvironmentRenderData{
			                     .skyboxMaterial = {}
		                     });
	}
	void RenderStorage::RemoveEnvironmentProxy(RID rid)
	{
		environments.erase(rid);
	}

	void RenderStorage::SetEnvironmentSkyboxMaterial(RID rid, RID material)
	{
		if (const auto& it = environments.find(rid); it != environments.end())
		{
			it->second.skyboxMaterial = material;
		}
	}

	void RenderStorage::SetEnvironmentVisible(RID rid, bool visible)
	{
		if (const auto& it = environments.find(rid); it != environments.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::RegisterLightProxy(RID rid)
	{
		lights.emplace(rid, LightRenderData{
			               .type = RendererLightType::Directional,
			               .transform = {},
			               .color = Color::WHITE,
			               .intensity = 1.0f,
			               .range = 100.0f,
			               .innerConeAngle = Math::Radians(30.0f),
			               .outerConeAngle = Math::Radians(45.0f),
			               .visible = true
		               });
	}

	void RenderStorage::RemoveLightProxy(RID rid)
	{
		lights.erase(rid);
	}

	void RenderStorage::SetLightTransform(RID rid, const Mat4& worldTransform)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetLightType(RID rid, RendererLightType type)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.type = type;
		}
	}

	void RenderStorage::SetLightColor(RID rid, const Color& color)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.color = color;
		}
	}

	void RenderStorage::SetLightIntensity(RID rid, f32 intensity)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.intensity = intensity;
		}
	}

	void RenderStorage::SetLightRange(RID rid, f32 range)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.range = range;
		}
	}

	void RenderStorage::SetLightInnerConeAngle(RID rid, f32 angle)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.innerConeAngle = Math::Radians(-angle);
		}
	}

	void RenderStorage::SetLightOuterConeAngle(RID rid, f32 angle)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.outerConeAngle = Math::Radians(-angle);
		}
	}

	void RenderStorage::SetLightVisible(RID rid, bool visible)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetLightEnableShadows(RID rid, bool enableShadows)
	{
		if (const auto& it = lights.find(rid); it != lights.end())
		{
			it->second.enableShadows = enableShadows;
		}
	}
}
