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
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	struct EditableSettings
	{
		String path;
		TypeID type;
	};
	struct SettingTypeResource
	{
		enum
		{
			Settings		//SubobjectList
		};
	};

	class SK_API Settings
	{
	public:
		static void CreateDefault(ArchiveWriter& writer, TypeID settingsType);
		static RID  Get(TypeID settingsType, TypeID typeId);
		static RID  Load(ArchiveReader&reader, TypeID settingsType);
		static void Save(ArchiveWriter& writer, TypeID settingsType);

		template <typename T1, typename T2>
		static RID Get()
		{
			return Get(TypeInfo<T1>::ID(), TypeInfo<T2>::ID());
		}
	};
}
