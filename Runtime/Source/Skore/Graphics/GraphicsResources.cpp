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

#include "GraphicsResources.hpp"

#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	RID ShaderResource::GetVariant(RID shader, StringView name)
	{
		RID retVariant = {};
		if (ResourceObject shaderObject = Resources::Read(shader))
		{
			shaderObject.IterateSubObjectList(Variants, [&](RID variant)
			{
				if (ResourceObject variantObject = Resources::Read(variant))
				{
					//TODO : break iteration here.
					if (variantObject.GetString(ShaderVariantResource::Name) == name)
					{
						retVariant = variant;
					}
				}
			});
		}
		return retVariant;
	}

	void CreateGraphicsDefaultValues()
	{
		//RID defaultMaterial = Resources::Create<TextureResource>();
		// ResourceObject defaultMaterialObject = Resources::Write(defaultMaterial);
		// defaultMaterialObject.SetColor(MaterialResource::BaseColor, Color::WHITE);
		// defaultMaterialObject.Commit();
		// Resources::FindType<MaterialResource>()->SetDefaultValue(defaultMaterial);
	}
}
