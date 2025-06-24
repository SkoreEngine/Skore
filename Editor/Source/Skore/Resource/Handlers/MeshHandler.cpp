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

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct MeshHandler : ResourceAssetHandler
	{
		SK_CLASS(MeshHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".mesh";
		}

		void OpenAsset(RID rid) override
		{
			//TODO
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<MeshResource>::ID();
		}

		RID Create(UUID uuid, UndoRedoScope* scope) override
		{
			RID mesh = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);

			ResourceObject meshObject = Resources::Write(mesh);
			meshObject.SetString(MeshResource::Name, "Mesh");
			meshObject.Commit(scope);

			return mesh;
		}

		StringView GetDesc() override
		{
			return "Mesh";
		}
	};

	void RegisterMeshHandler()
	{
		Reflection::Type<MeshHandler>();
	}
}
