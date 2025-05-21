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
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/UUID.hpp"


namespace Skore
{
	class Asset;
}

namespace Skore
{
	class AssetFile;

	enum class AssetStatus
	{
		None,
		Warning,
		Error
	};


	struct AssetImporter : Object
	{
		SK_CLASS(AssetImporter, Object)

		virtual Array<String> ImportExtensions() = 0;

		virtual Array<String> AssociatedExtensions()
		{
			return {};
		}

		virtual bool   ImportAsset(AssetFile* assetFile, StringView path) = 0;
		virtual TypeID GetAssetTypeId() = 0;
	};

	struct AssetHandler : Object
	{
		SK_CLASS(AssetHandler, Object)

		virtual TypeID GetAssetTypeId() = 0;
		virtual void   OpenAsset(AssetFile* assetFile) {}
		virtual void   LoadInstance(AssetFile* assetFile, Asset* asset);

		virtual Array<String> AssociatedExtensions()
		{
			return {};
		}

		virtual String Extension()
		{
			return "";
		}

		virtual String Name() = 0;
	};

	struct AssetInfo : Object
	{
		SK_CLASS(AssetInfo, Object)

		UUID   uuid;
		String type;
		String name;
		u64    offset;
		u64    size;

		static void RegisterType(NativeReflectType<AssetInfo>& type);
	};

	struct AssetInternalInfo : Object
	{
		SK_CLASS(AssetInternalInfo, Object)

		UUID          uuid;
		String        status;
		Array<String> missingFiles;

		static void RegisterType(NativeReflectType<AssetInternalInfo>& type);
	};
}
