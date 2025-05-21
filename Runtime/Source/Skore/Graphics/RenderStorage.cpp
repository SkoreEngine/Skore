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
#include "Skore/Scene/Components/LightComponent.hpp"

namespace Skore
{
	void RenderStorage::RegisterMeshProxy(VoidPtr ptr)
	{
		meshes.emplace(PtrToInt(ptr), MeshRenderData{
			               .mesh = nullptr,
			               .transform = {},
			               .materials = {},
			               .visible = true,
		               });
	}


	void RenderStorage::RemoveMeshProxy(VoidPtr ptr)
	{
		meshes.erase(PtrToInt(ptr));
	}

	void RenderStorage::SetMeshTransform(VoidPtr ptr, const Mat4& worldTransform)
	{
		if (const auto& it = meshes.find(PtrToInt(ptr)); it != meshes.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetMesh(VoidPtr ptr, MeshAsset* meshAsset)
	{
		if (const auto& it = meshes.find(PtrToInt(ptr)); it != meshes.end())
		{
			it->second.mesh = meshAsset;
		}
	}

	void RenderStorage::SetMeshVisible(VoidPtr ptr, bool visible)
	{
		if (const auto& it = meshes.find(PtrToInt(ptr)); it != meshes.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetMeshMaterials(VoidPtr ptr, Span<MaterialAsset*> materials)
	{
		if (const auto& it = meshes.find(PtrToInt(ptr)); it != meshes.end())
		{
			it->second.materials = materials;
		}
	}

	void RenderStorage::SetMeshCastShadows(MeshRenderComponent* meshRenderComponent, bool castShadows)
	{
		if (const auto& it = meshes.find(PtrToInt(meshRenderComponent)); it != meshes.end())
		{
			it->second.castShadows = castShadows;
		}
	}

	void RenderStorage::RegisterEnvironmentProxy(VoidPtr ptr)
	{
		environments.emplace(PtrToInt(ptr), EnvironmentRenderData{
			                     .skyboxMaterial = nullptr
		                     });
	}
	void RenderStorage::RemoveEnvironmentProxy(VoidPtr ptr)
	{
		environments.erase(PtrToInt(ptr));
	}

	void RenderStorage::SetEnvironmentSkyboxMaterial(VoidPtr ptr, MaterialAsset* material)
	{
		if (const auto& it = environments.find(PtrToInt(ptr)); it != environments.end())
		{
			it->second.skyboxMaterial = material;
		}
	}

	void RenderStorage::SetEnvironmentVisible(VoidPtr ptr, bool visible)
	{
		if (const auto& it = environments.find(PtrToInt(ptr)); it != environments.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::RegisterLightProxy(VoidPtr ptr)
	{
		lights.emplace(PtrToInt(ptr), LightRenderData{
			               .type = LightTypeData::Directional,
			               .transform = {},
			               .color = Color::WHITE,
			               .intensity = 1.0f,
			               .range = 100.0f,
			               .innerConeAngle = Math::Radians(30.0f),
			               .outerConeAngle = Math::Radians(45.0f),
			               .visible = true
		               });
	}

	void RenderStorage::RemoveLightProxy(VoidPtr ptr)
	{
		lights.erase(PtrToInt(ptr));
	}

	void RenderStorage::SetLightTransform(VoidPtr ptr, const Mat4& worldTransform)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetLightType(VoidPtr ptr, LightComponent::LightType type)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			switch (type)
			{
				case LightComponent::LightType::Directional:
					it->second.type = LightTypeData::Directional;
					break;
				case LightComponent::LightType::Point:
					it->second.type = LightTypeData::Point;
					break;
				case LightComponent::LightType::Spot:
					it->second.type = LightTypeData::Spot;
					break;
			}
		}
	}

	void RenderStorage::SetLightColor(VoidPtr ptr, const Color& color)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.color = color;
		}
	}

	void RenderStorage::SetLightIntensity(VoidPtr ptr, f32 intensity)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.intensity = intensity;
		}
	}

	void RenderStorage::SetLightRange(VoidPtr ptr, f32 range)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.range = range;
		}
	}

	void RenderStorage::SetLightInnerConeAngle(VoidPtr ptr, f32 angle)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.innerConeAngle = Math::Radians(-angle);
		}
	}

	void RenderStorage::SetLightOuterConeAngle(VoidPtr ptr, f32 angle)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.outerConeAngle = Math::Radians(-angle);
		}
	}

	void RenderStorage::SetLightVisible(VoidPtr ptr, bool visible)
	{
		if (const auto& it = lights.find(PtrToInt(ptr)); it != lights.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetLightEnableShadows(LightComponent* lightComponent, bool enableShadows)
	{
		if (const auto& it = lights.find(PtrToInt(lightComponent)); it != lights.end())
		{
			it->second.enableShadows = enableShadows;
		}
	}
}
