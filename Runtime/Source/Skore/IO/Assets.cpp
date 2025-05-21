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

#include "Assets.hpp"

#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Ref.hpp"

namespace Skore
{
	namespace
	{
		struct AssetStorage
		{
			AssetStorage() = default;
			SK_NO_COPY_CONSTRUCTOR(AssetStorage);

			AssetInterface* interface = {};
			String currentPath = {};
		};

		HashMap<UUID, Ref<AssetStorage>>   assets = {};
		HashMap<String, Ref<AssetStorage>> assetsByPath = {};
	}

	void AssetsShutdown()
	{
		assets.Clear();
		assetsByPath.Clear();
	}

	UUID Asset::GetUUID() const
	{
		if (interface)
		{
			return interface->GetUUID();
		}
		return {};
	}

	StringView Asset::GetName() const
	{
		if (interface)
		{
			return interface->GetName();
		}
		return "";
	}

	void Asset::RegisterType(NativeReflectType<Asset>& type)
	{
		type.Function<&Asset::GetUUID>("GetUUID");
		type.Function<&Asset::GetName>("GetName");
		type.Function<&Asset::Changed>("Changed");
	}

	void Assets::Register(StringView path, UUID uuid, AssetInterface* interface)
	{
		Ref<AssetStorage> storage = MakeRef<AssetStorage>();
		storage->currentPath = path;
		storage->interface = interface;

		SK_ASSERT(!path.Empty() || uuid, "Asset path or UUID must be set!");

		if (uuid)
		{
			assets.Insert(uuid, storage);
		}

		if (!path.Empty())
		{
			assetsByPath.Insert(String(path), storage);
		}
	}

	void Assets::UpdatePath(UUID uuid, StringView path)
	{
		if (const auto& it = assets.Find(uuid))
		{
			assetsByPath.Erase(it->second->currentPath);
			it->second->currentPath = path;
			assetsByPath.Insert(it->second->currentPath, it->second);
		}
	}

	Asset* Assets::GetByPath(StringView path)
	{
		if (const auto& it = assetsByPath.Find(path))
		{
			return it->second->interface->GetInstance();
		}
		return {};
	}

	AssetInterface* Assets::GetInterface(UUID id)
	{
		if (const auto& it = assets.Find(id))
		{
			return it->second->interface;
		}
		return {};
	}

	AssetInterface* Assets::GetInterfaceByPath(StringView path)
	{
		if (const auto& it = assetsByPath.Find(path))
		{
			return it->second->interface;
		}
		return {};
	}

	Asset* Assets::Get(UUID id)
	{
		if (const auto& it = assets.Find(id))
		{
			return it->second->interface->GetInstance();
		}
		return {};
	}
}
