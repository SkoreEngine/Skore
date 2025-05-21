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

#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	struct AssetInterface;

	class SK_API Asset : public Object
	{
	public:
		SK_CLASS(Asset, Object)

		virtual void Changed() {}

		UUID GetUUID() const;
		StringView GetName() const;

		static void RegisterType(NativeReflectType<Asset>& type);
		friend struct Assets;

		AssetInterface* interface = nullptr;
	};

	struct SK_API AssetInterface
	{
		virtual            ~AssetInterface() = default;
		virtual UUID       GetUUID() const = 0;
		virtual StringView GetName() const = 0;
		virtual StringView GetAbsolutePath() const = 0;
		virtual Asset*     GetInstance() = 0;
	};

	struct SK_API Assets
	{
		static void   Register(StringView path, UUID uuid, AssetInterface* interface);
		static void   UpdatePath(UUID uuid, StringView path);
		static Asset* Get(UUID id);
		static Asset* GetByPath(StringView path);

		static AssetInterface* GetInterface(UUID id);
		static AssetInterface* GetInterfaceByPath(StringView path);


		template <typename T>
		static T* GetByPath(StringView path)
		{
			return static_cast<T*>(GetByPath(path));
		}

		template <typename T>
		static T* Get(UUID uuid)
		{
			return static_cast<T*>(Get(uuid));
		}
	};

	template <typename T>
	struct SerializeField<T*, Traits::EnableIf<Traits::IsBaseOf<Asset, T>>>
	{
		constexpr static bool hasSpecialization = true;

		static void Write(ArchiveWriter& writer, StringView name, T* const * value)
		{
			if (const T* asset = *value)
			{
				writer.WriteString(name, asset->GetUUID().ToString());
			}
		}

		static void Get(ArchiveReader& reader, T** value)
		{
			if (UUID uuid = UUID::FromString(reader.GetString()))
			{
				*value = Assets::Get<T>(uuid);
			}
		}

		static void Add(ArchiveWriter& writer, T* const * value)
		{
			if (const T* asset = *value)
			{
				writer.AddString(asset->GetUUID().ToString());
			}
		}
	};
}
