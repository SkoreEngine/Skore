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

	class SK_API SettingsItem
	{
	public:
		SettingsItem(TypeID settingType);
		~SettingsItem();

		void                SetLabel(StringView label);
		StringView          GetLabel() const;
		void                AddChild(SettingsItem* child);
		ResourceType*       GetType() const;
		void                SetType(ResourceType* type);
		RID                 GetRID() const;
		Span<SettingsItem*> GetChildren() const;

		void Instantiate();

	private:
		TypeID               settingType;
		String               label;
		RID                  rid{};
		ResourceType*        type{};
		Array<SettingsItem*> children;
	};

	class SK_API Settings
	{
	public:
		static void                Init(TypeID settingType);
		static Span<SettingsItem*> GetItems(TypeID settingType);

		static RID  GetSetting(TypeID settingType, TypeID typeId);
		static RID  Load(ArchiveReader&reader, TypeID settingType);
		static void Save(ArchiveWriter& writer, TypeID settingType);
	};
}
