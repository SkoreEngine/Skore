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

#include "Object.hpp"
#include "Ref.hpp"
#include "Span.hpp"
#include "String.hpp"
#include "StringView.hpp"
#include "TypeInfo.hpp"
#include "UUID.hpp"

namespace Skore
{
	class ReflectType;

	struct SK_API ArchiveWriter
	{
		virtual ~ArchiveWriter() = default;

		virtual void WriteBool(StringView name, bool value) = 0;
		virtual void WriteInt(StringView name, i64 value) = 0;
		virtual void WriteUInt(StringView name, u64 value) = 0;
		virtual void WriteFloat(StringView name, f64 value) = 0;
		virtual void WriteString(StringView name, StringView value) = 0;
		virtual void WriteBlob(StringView name, ConstPtr data, u64 size) = 0;

		virtual void AddBool(bool value) = 0;
		virtual void AddInt(i64 value) = 0;
		virtual void AddUInt(u64 value) = 0;
		virtual void AddFloat(f64 value) = 0;
		virtual void AddString(StringView value) = 0;
		virtual void AddBlob(ConstPtr data, u64 size) = 0;

		virtual void BeginMap() = 0;
		virtual void BeginMap(StringView name) = 0;
		virtual void EndMap() = 0;

		virtual void BeginSeq() = 0;
		virtual void BeginSeq(StringView name) = 0;
		virtual void EndSeq() = 0;
	};

	struct SK_API ArchiveReader
	{
		virtual ~ArchiveReader() = default;

		virtual bool       ReadBool(StringView name) = 0;
		virtual i64        ReadInt(StringView name) = 0;
		virtual u64        ReadUInt(StringView name) = 0;
		virtual f64        ReadFloat(StringView name) = 0;
		virtual StringView ReadString(StringView name) = 0;

		virtual bool       GetBool() = 0;
		virtual i64        GetInt() = 0;
		virtual u64        GetUInt() = 0;
		virtual f64        GetFloat() = 0;
		virtual StringView GetString() = 0;
		virtual Span<u8>   GetBlob() = 0;

		virtual void BeginSeq() = 0;
		virtual bool BeginSeq(StringView name) = 0;
		virtual bool NextSeqEntry() = 0;
		virtual void EndSeq() = 0;

		virtual void       BeginMap() = 0;
		virtual bool       BeginMap(StringView name) = 0;
		virtual bool       NextMapEntry() = 0;
		virtual StringView GetCurrentKey() = 0;
		virtual void       EndMap() = 0;
	};

	class SK_API YamlArchiveWriter : public ArchiveWriter
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(YamlArchiveWriter);

		YamlArchiveWriter();
		~YamlArchiveWriter() override;

		void WriteBool(StringView name, bool value) override;
		void WriteInt(StringView name, i64 value) override;
		void WriteUInt(StringView name, u64 value) override;
		void WriteFloat(StringView name, f64 value) override;
		void WriteString(StringView name, StringView value) override;
		void WriteBlob(StringView name, ConstPtr data, u64 size) override;

		void AddBool(bool value) override;
		void AddInt(i64 value) override;
		void AddUInt(u64 value) override;
		void AddFloat(f64 value) override;
		void AddString(StringView value) override;
		void AddBlob(ConstPtr data, u64 size) override;

		void BeginSeq() override;
		void BeginSeq(StringView name) override;
		void EndSeq() override;

		void BeginMap() override;
		void BeginMap(StringView name) override;
		void EndMap() override;

		String   EmitAsString() const;
		Span<u8> GetBlobs() const;

		struct YamlContext;

		YamlContext* m_context = nullptr;
	private:
		char      m_contextData[288]{};
		Array<u8> m_blobs;
	};


	class SK_API YamlArchiveReader : public ArchiveReader
	{
	public:
		YamlArchiveReader(StringView yaml, Span<u8> blobs = {});

		bool       ReadBool(StringView name) override;
		i64        ReadInt(StringView name) override;
		u64        ReadUInt(StringView name) override;
		f64        ReadFloat(StringView name) override;
		StringView ReadString(StringView name) override;
		bool       GetBool() override;
		i64        GetInt() override;
		u64        GetUInt() override;
		f64        GetFloat() override;
		StringView GetString() override;
		Span<u8>   GetBlob() override;
		void       BeginSeq() override;
		bool       BeginSeq(StringView name) override;
		bool       NextSeqEntry() override;
		void       EndSeq() override;
		void       BeginMap() override;
		bool       BeginMap(StringView name) override;
		bool       NextMapEntry() override;
		StringView GetCurrentKey() override;
		void       EndMap() override;

		struct YamlContext;

	private:
		char         m_contextData[296]{};
		YamlContext* m_context = nullptr;
		Array<u8>    m_blobs;
	};


	//- TODO JsonArchiveWriter are not finished.
	class SK_API JsonArchiveWriter : public ArchiveWriter
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(JsonArchiveWriter);
		JsonArchiveWriter();
		~JsonArchiveWriter() override;

		void WriteBool(StringView name, bool value) override;
		void WriteInt(StringView name, i64 value) override;
		void WriteUInt(StringView name, u64 value) override;
		void WriteFloat(StringView name, f64 value) override;
		void WriteString(StringView name, StringView value) override;
		void WriteBlob(StringView name, ConstPtr data, u64 size) override;
		void AddBool(bool value) override;

		void AddInt(i64 value) override;
		void AddUInt(u64 value) override;
		void AddFloat(f64 value) override;
		void AddString(StringView value) override;
		void AddBlob(ConstPtr data, u64 size) override;

		void BeginMap() override;
		void BeginMap(StringView name) override;
		void EndMap() override;

		void BeginSeq() override;
		void BeginSeq(StringView name) override;
		void EndSeq() override;

		String EmitAsString() const;
	private:
		VoidPtr m_doc;
		VoidPtr m_current;
	};

	class SK_API BinaryArchiveWriter : public ArchiveWriter
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(BinaryArchiveWriter);

		BinaryArchiveWriter();
		~BinaryArchiveWriter() override;

		void WriteBool(StringView name, bool value) override;
		void WriteInt(StringView name, i64 value) override;
		void WriteUInt(StringView name, u64 value) override;
		void WriteFloat(StringView name, f64 value) override;
		void WriteString(StringView name, StringView value) override;
		void WriteBlob(StringView name, ConstPtr data, u64 size) override;

		void AddBool(bool value) override;
		void AddInt(i64 value) override;
		void AddUInt(u64 value) override;
		void AddFloat(f64 value) override;
		void AddString(StringView value) override;
		void AddBlob(ConstPtr data, u64 size) override;

		void BeginSeq() override;
		void BeginSeq(StringView name) override;
		void EndSeq() override;

		void BeginMap() override;
		void BeginMap(StringView name) override;
		void EndMap() override;

		Span<u8> GetData() const;

	private:
		struct BinaryStack
		{
			u64 initData;
		};

		Array<u8>          m_data;
		Array<BinaryStack> m_stack;

		void WriteMapData(StringView name, ConstPtr value, u64 size);
		void WriteName(StringView name);
		void WriteValue(ConstPtr value, u64 size);

		void PopStack();
	};

	class SK_API BinaryArchiveReader : public ArchiveReader
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(BinaryArchiveReader);

		BinaryArchiveReader(Span<u8> data);
		~BinaryArchiveReader() override = default;

		bool       ReadBool(StringView name) override;
		i64        ReadInt(StringView name) override;
		u64        ReadUInt(StringView name) override;
		f64        ReadFloat(StringView name) override;
		StringView ReadString(StringView name) override;

		bool       GetBool() override;
		i64        GetInt() override;
		u64        GetUInt() override;
		f64        GetFloat() override;
		StringView GetString() override;
		Span<u8>   GetBlob() override;

		void BeginSeq() override;
		bool BeginSeq(StringView name) override;
		bool NextSeqEntry() override;
		void EndSeq() override;

		void       BeginMap() override;
		bool       BeginMap(StringView name) override;
		bool       NextMapEntry() override;
		StringView GetCurrentKey() override;
		void       EndMap() override;

	private:
		enum class IterType
		{
			None = 0,
			Map  = 1,
			Seq  = 2
		};

		struct MapField
		{
			StringView name;
			u64        pos;
			u64        size;
		};

		struct ValueOffset
		{
			u64 pos;
			u64 size;
		};

		struct BinaryStack
		{
			u64      begin;
			u64      end;
			u64      iter;
			IterType type;
		};

		Span<u8>           m_data;
		Array<BinaryStack> m_stack;

		void ReadOffset(ValueOffset& data, const BinaryStack& stack) const;

		void ReadMapField(MapField& data, u64 offset) const;
		bool ReadMapField(MapField& data, StringView name) const;
	};

	namespace Serialization
	{
		SK_API void Serialize(const ReflectType* reflectType, ArchiveWriter& writer, ConstPtr instance);
		SK_API void Deserialize(const ReflectType* reflectType, ArchiveReader& reader, VoidPtr instance);

		SK_API StringView GetEnumDesc(const ReflectType* reflectType, ConstPtr value);
		SK_API ConstPtr GetEnumValue(const ReflectType* reflectType, StringView desc);


		//this is different from Reflection::FindTypeById
		//it won't return the type if it's not valid for serialization, even if it's registered
		SK_API ReflectType* FindTypeToSerialize(TypeID typeId);
	}

	template <typename T, typename = void>
	struct SerializeField
	{
		constexpr static bool hasSpecialization = false;

		static void Write(ArchiveWriter& writer, StringView name, const T* value)
		{
			if (ReflectType* type = Serialization::FindTypeToSerialize(TypeInfo<T>::ID()))
			{
				writer.BeginMap(name);
				Serialization::Serialize(type, writer, value);
				writer.EndMap();
			}
		}

		static void Get(ArchiveReader& reader, T* value)
		{
			if (ReflectType* type = Serialization::FindTypeToSerialize(TypeInfo<T>::ID()))
			{
				reader.BeginMap();
				Serialization::Deserialize(type, reader, value);
				reader.EndMap();
			}
		}

		static void Add(ArchiveWriter& writer, const T* value)
		{
			if (ReflectType* type = Serialization::FindTypeToSerialize(TypeInfo<T>::ID()))
			{
				writer.BeginMap();
				Serialization::Serialize(type, writer, value);
				writer.EndMap();
			}
		}
	};

	template <typename T>
	struct SerializeField<T, Traits::EnableIf<Traits::IsBaseOf<Object, T>>>
	{
		constexpr static bool hasSpecialization = true;

		static void Write(ArchiveWriter& writer, StringView name, const T* value)
		{
			writer.BeginMap(name);
			value->Serialize(writer);
			writer.EndMap();
		}

		static void Get(ArchiveReader& reader, T* value)
		{
			reader.BeginMap();
			value->Deserialize(reader);
			reader.EndMap();
		}

		static void Add(ArchiveWriter& writer, const T* value)
		{
			writer.BeginMap();
			value->Serialize(writer);
			writer.EndMap();
		}
	};

#define DEFINE_SERIALIZER(TYPE, TYPE_NAME) \
template <> \
struct SerializeField<TYPE> \
{ \
constexpr static bool hasSpecialization = true;								 \
static void Write(ArchiveWriter& writer, StringView name, const TYPE* value) \
{ \
writer.Write##TYPE_NAME(name, *value); \
} \
\
static void Get(ArchiveReader& reader, TYPE* value) \
{ \
*value = reader.Get##TYPE_NAME(); \
} \
\
static void Add(ArchiveWriter& writer, const TYPE* value) \
{ \
	writer.Add##TYPE_NAME(*value); \
} \
};
	// For unsigned integers
	DEFINE_SERIALIZER(u8, UInt);
	DEFINE_SERIALIZER(u16, UInt);
	DEFINE_SERIALIZER(u32, UInt);
	DEFINE_SERIALIZER(u64, UInt);

	// For signed integers
	DEFINE_SERIALIZER(i8, Int);
	DEFINE_SERIALIZER(i16, Int);
	DEFINE_SERIALIZER(i32, Int);
	DEFINE_SERIALIZER(i64, Int);
	DEFINE_SERIALIZER(f32, Float);
	DEFINE_SERIALIZER(f64, Float);
	DEFINE_SERIALIZER(bool, Bool);

	template <usize BufferSize>
	struct SerializeField<BasicString<char, BufferSize>>
	{
		constexpr static bool hasSpecialization = true;

		using T = BasicString<char, BufferSize>;

		static void Write(ArchiveWriter& writer, StringView name, const T* value)
		{
			if (!value->Empty())
			{
				writer.WriteString(name, StringView(*value));
			}

		}

		static void Get(ArchiveReader& reader, T* value)
		{
			*value = reader.GetString();
		}

		static void Add(ArchiveWriter& writer, const T* value)
		{
			if (!value->Empty())
			{
				writer.AddString(StringView(*value));
			}
		}
	};

	template <>
	struct SerializeField<UUID>
	{
		constexpr static bool hasSpecialization = true;

		static void Write(ArchiveWriter& writer, StringView name, const UUID* value)
		{
			writer.WriteString(name, value->ToString());
		}

		static void Get(ArchiveReader& reader, UUID* value)
		{
			*value = UUID::FromString(reader.GetString());
		}

		static void Add(ArchiveWriter& writer, const UUID* value)
		{
			writer.AddString(value->ToString());
		}
	};

	template <typename T>
	struct SerializeField<T, Traits::EnableIf<Traits::IsEnum<T>>>
	{
		constexpr static bool hasSpecialization = true;

		static void Write(ArchiveWriter& writer, StringView name, const T* value)
		{
			if (String desc = Serialization::GetEnumDesc(Serialization::FindTypeToSerialize(TypeInfo<T>::ID()), value); !desc.Empty())
			{
				writer.WriteString(name, desc);
			}
		}

		static void Get(ArchiveReader& reader, T* value)
		{
			if (ConstPtr valuePtr = Serialization::GetEnumValue(Serialization::FindTypeToSerialize(TypeInfo<T>::ID()), reader.GetString()))
			{
				*value = *static_cast<const T*>(valuePtr);
			}
		}

		static void Add(ArchiveWriter& writer, const T* value)
		{
			if (String desc = Serialization::GetEnumDesc(Serialization::FindTypeToSerialize(TypeInfo<T>::ID()), value); !desc.Empty())
			{
				writer.AddString(desc);
			}
		}
	};

	template <>
	struct SerializeField<Array<u8>>
	{
		constexpr static bool hasSpecialization = true;

		static void Write(ArchiveWriter& writer, StringView name, const Array<u8>* value)
		{
			writer.WriteBlob(name, value->begin(), value->Size());
		}

		static void Add(ArchiveWriter& writer, const Array<u8>* value)
		{
			writer.AddBlob(value->begin(), value->Size());
		}

		static void Get(ArchiveReader& reader, Array<u8>* value)
		{
			Span<u8> blob = reader.GetBlob();
			value->Append(blob.begin(), blob.Size());
		}
	};

	template <typename T>
	struct SerializeField<Array<T>>
	{
		constexpr static bool hasSpecialization = true;

		using Arr = Array<T>;

		static void Write(ArchiveWriter& writer, StringView name, const Arr* value)
		{
			const Array<T> arr = *value;
			if (!arr.Empty())
			{
				if constexpr (SerializeField<T>::hasSpecialization)
				{
					writer.BeginSeq(name);
					for (const auto& v : arr)
					{
						SerializeField<T>::Add(writer, &v);
					}
					writer.EndSeq();
					return;
				}

				if (ReflectType* type = Serialization::FindTypeToSerialize(TypeInfo<T>::ID()))
				{
					writer.BeginSeq(name);
					for (const auto& v : arr)
					{
						writer.BeginMap();
						Serialization::Serialize(type, writer, &v);
						writer.EndMap();
					}
					writer.EndSeq();
				}
			}
		}

		static void Get(ArchiveReader& reader, Arr* value)
		{
			Array<T>& arr = *value;
			if constexpr (SerializeField<T>::hasSpecialization)
			{
				reader.BeginSeq();
				while (reader.NextSeqEntry())
				{
					T& vl = arr.EmplaceBack();
					SerializeField<T>::Get(reader, &vl);
				}
				reader.EndSeq();
				return;
			}

			if (ReflectType* type = Serialization::FindTypeToSerialize(TypeInfo<T>::ID()))
			{
				reader.BeginSeq();
				while (reader.NextSeqEntry())
				{
					T& vl = arr.EmplaceBack();
					reader.BeginMap();
					Serialization::Deserialize(type, reader, &vl);
					reader.EndMap();
				}
				reader.EndSeq();
			}
		}

		static void Add(ArchiveWriter& writer, const Arr* value)
		{
			const Array<T> arr = *value;

			if (!arr.Empty())
			{
				if constexpr (SerializeField<T>::hasSpecialization)
				{
					writer.BeginSeq();
					for (const auto& v : arr)
					{
						SerializeField<T>::Add(writer, &v);
					}
					writer.EndSeq();
					return;
				}

				if (ReflectType* type = Serialization::FindTypeToSerialize(TypeInfo<T>::ID()))
				{
					writer.BeginSeq();
					for (const auto& v : arr)
					{
						Serialization::Serialize(type, writer, &v);
					}
					writer.EndSeq();
				}
			}
		}
	};
}
