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

#include "ResourceCommon.hpp"
#include "ResourceObject.hpp"
#include "ResourceType.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	class SK_API Resources
	{
	public:
		//type api
		static ResourceTypeBuilder Type(TypeID typeId, StringView name);
		static ResourceType*       FindTypeByID(TypeID typeId);
		static ResourceType*       FindTypeByName(StringView name);

		template <typename T>
		static ResourceTypeBuilder Type(StringView name)
		{
			return Type(TypeInfo<T>::ID(), name);
		}

		template <typename T>
		static ResourceTypeBuilder Type()
		{
			return Type(TypeInfo<T>::ID(), TypeInfo<T>::Name());
		}

		template <typename T>
		static ResourceType* FindType()
		{
			return FindTypeByID(TypeInfo<T>::ID());
		}

		//resource API
		static RID              Create(TypeID typeId, UUID uuid = {}, UndoRedoScope* scope = nullptr);
		static RID              CreateFromPrototype(RID rid, UUID uuid = {}, UndoRedoScope* scope = nullptr);
		static ResourceStorage* GetStorage(RID rid);
		static ResourceObject   Write(RID rid);
		static ResourceObject   Read(RID rid);
		static bool             HasValue(RID rid);
		static RID              GetParent(RID rid);
		static RID              GetPrototype(RID rid);
		static RID              Clone(RID rid, UUID uuid = {}, UndoRedoScope* scope = nullptr);
		static void             Reset(RID rid, UndoRedoScope* scope = nullptr);
		static void             Destroy(RID rid, UndoRedoScope* scope = nullptr);
		static u64              GetVersion(RID rid);
		static UUID             GetUUID(RID rid);
		static ResourceType*    GetType(RID rid);
		static RID              FindByUUID(const UUID& uuid);
		static RID              FindOrReserveByUUID(const UUID& uuid);
		static bool             IsParentOf(RID parent, RID child);

		//path
		static void       SetPath(RID rid, StringView path);
		static StringView GetPath(RID rid);
		static RID        FindByPath(StringView path);


		static Span<RID> GetResourceByType(TypeID typeId);

		static void Serialize(RID rid, ArchiveWriter& writer);
		static RID  Deserialize(ArchiveReader& reader, UndoRedoScope* scope = nullptr);

		static bool ToResource(RID rid, ConstPtr instance, UndoRedoScope* scope = nullptr);
		static bool FromResource(RID rid, VoidPtr instance);
		static bool FromResource(const ResourceObject& object, VoidPtr instance);

		static Array<CompareSubObjectListResult> CompareSubObjectList(const ResourceObject& oldObject, const ResourceObject& newObject, u32 index);
		static void CompareSubObjectList(const ResourceObject& oldObject, const ResourceObject& newObject, u32 index, VoidPtr userData, FnCompareSubObjectListCallback callback);

		template <typename T>
		static RID Create(UUID uuid = {}, UndoRedoScope* scope = nullptr)
		{
			return Create(TypeInfo<T>::ID(), uuid, scope);
		}

		static UndoRedoScope* CreateScope(StringView name = "");
		static void           DestroyScope(UndoRedoScope* scope);
		static void           Undo(UndoRedoScope* scope);
		static void           Redo(UndoRedoScope* scope);
		static StringView     GetScopeName(UndoRedoScope* scope);

		static void GarbageCollect();

	private:
		static ResourceType* CreateFromReflectType(ReflectType* reflectType);
	};
}
