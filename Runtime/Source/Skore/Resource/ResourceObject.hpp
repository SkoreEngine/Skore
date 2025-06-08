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
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	struct ResourceInstanceInfo;
	class ResourceType;
	struct UndoRedoScope;

	class SK_API ResourceObject
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(ResourceObject);

		ResourceObject(ResourceStorage* storage, ResourceInstance writeInstance);
		~ResourceObject();

		void SetBool(u32 index, bool value);
		void SetInt(u32 index, i64 value);
		void SetUInt(u32 index, u64 value);
		void SetFloat(u32 index, f64 value);
		void SetString(u32 index, StringView value);
		void SetVec2(u32 index, Vec2 value);
		void SetVec3(u32 index, Vec3 value);
		void SetVec4(u32 index, Vec4 value);
		void SetQuat(u32 index, Quat value);
		void SetColor(u32 index, Color value);
		void SetEnum(u32 index, i64 enumValue);
		void SetBlob(u32 index, Span<u8> bytes);
		void SetReference(u32 index, RID rid);
		void SetReferenceArray(u32 index, Span<RID> refs);
		void AddToReferenceArray(u32 index, RID ref);
		void ClearReferenceArray(u32 index);
		void SetSubObject(u32 index, RID subObject);

		//subobjects
		void AddToSubObjectSet(u32 index, RID subObject);
		void AddToSubObjectSet(u32 index, Span<RID> subObjects);
		void RemoveFromSubObjectSet(u32 index, RID subObject);
		void RemoveFromSubObjectSet(u32 index, const Span<RID>& subObjects);
		void ClearSubObjectSet(u32 index);

		//subobject prototypes.
		usize GetRemoveFromPrototypeSubObjectSetCount(u32 index) const;
		void  GetRemoveFromPrototypeSubObjectSet(u32 index, Span<RID> remove) const;
		void  RemoveFromPrototypeSubObjectSet(u32 index, RID remove);
		void  RemoveFromPrototypeSubObjectSet(u32 index, const Span<RID>& remove);
		void  CancelRemoveFromPrototypeSubObjectSet(u32 index, RID remove);
		void  CancelRemoveFromPrototypeSubObjectSet(u32 index, const Span<RID>& remove);
		void  ClearRemoveFromPrototypeSubObjectSet(u32 index);

		//if index = SubobjectSet, remove from set
		//if index = subobject, compare the values, if equals, cleanup.
		void RemoveSubObject(u32 index, RID rid);

		bool       HasValue(u32 index) const;
		bool       HasValueOnThisObject(u32 index) const;
		bool       GetBool(u32 index) const;
		i64        GetInt(u32 index) const;
		u64        GetUInt(u32 index) const;
		f64        GetFloat(u32 index) const;
		StringView GetString(u32 index) const;
		Vec2       GetVec2(u32 index) const;
		Vec3       GetVec3(u32 index) const;
		Vec4       GetVec4(u32 index) const;
		Quat       GetQuat(u32 index) const;
		Color      GetColor(u32 index) const;
		i64        GetEnum(u32 index) const;
		RID        GetSubObject(u32 index) const;
		RID        GetReference(u32 index) const;
		Span<u8>   GetBlob(u32 index) const;
		Span<RID>  GetReferenceArray(u32 index) const;
		bool	   HasOnReferenceArray(u32 index, RID rid) const;
		usize      GetSubObjectSetCount(u32 index) const;
		void       GetSubObjectSet(u32 index, Span<RID> subObjects) const;
		Array<RID> GetSubObjectSetAsArray(u32 index) const;
		bool       HasSubObjectSet(u32 index, RID rid) const;
		void       IterateSubObjectSet(u32 index, bool prototypeIterate, FnRIDCallback callback, VoidPtr userData) const;

		RID              GetRID() const;
		RID              GetPrototype() const;
		UUID             GetUUID() const;
		ResourceType*    GetType() const;
		ResourceStorage* GetStorage() const;
		u64				 GetVersion() const;

		void Commit(UndoRedoScope* scope = nullptr);

		template <typename T>
		void IterateSubObjectSet(u32 index, bool prototypeIterate, T&& func) const
		{
			IterateSubObjectSet(index, prototypeIterate, [](RID rid, VoidPtr userData)
			{
				auto& func = *static_cast<Traits::RemoveAll<T>*>(userData);
				return func(rid);
			}, &func);
		}

		explicit operator bool() const;

	private:
		ResourceStorage* m_storage;
		ResourceInstance m_currentInstance;

		void     SetValue(u32 index, ConstPtr value, usize size) const;
		void     UpdateHasValue(u32 index, bool hasValue) const;
		ConstPtr GetPtr(u32 index) const;
		VoidPtr  GetMutPtr(u32 index) const;

		bool	 ValidSubObjectOnSet(const ResourceStorage* readingStorage, u32 index, RID rid) const;

		template <typename T>
		const T* GetPtr(u32 index) const
		{
			return static_cast<const T*>(GetPtr(index));
		}

		template <typename T>
		T* GetMutPtr(u32 index) const
		{
			T* ptr = static_cast<T*>(GetMutPtr(index));
			if (ptr && !HasValueOnThisObject(index))
			{
				new(ptr) T{};
				UpdateHasValue(index, true);
			}
			return ptr;
		}
	};
}
