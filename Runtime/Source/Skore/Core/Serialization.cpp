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

#include "Serialization.hpp"

#define RYML_SINGLE_HDR_DEFINE_NOW
#include <optional>

#include "rapidyaml.hpp"
#include "Reflection.hpp"
#include "yyjson.h"

namespace Skore
{
	namespace
	{
		VoidPtr Malloc(VoidPtr ctx, usize size)
		{
			return MemAlloc(size);
		}

		VoidPtr Realloc(VoidPtr ctx, VoidPtr ptr, usize oldSize, usize size)
		{
			return MemRealloc(ptr, size);
		}

		void Free(VoidPtr ctx, VoidPtr ptr)
		{
			MemFree(ptr);
		}

		yyjson_alc alloc{
			.malloc = Malloc,
			.realloc = Realloc,
			.free = Free,
			.ctx = nullptr
		};
	}

	struct YamlArchiveWriter::YamlContext
	{
		ryml::Tree    tree;
		ryml::NodeRef current;
	};


	YamlArchiveWriter::YamlArchiveWriter()
	{
		static_assert(sizeof(YamlContext) <= sizeof(m_contextData), "YamlContext is smaller than context buffer ");
		m_context = reinterpret_cast<YamlContext*>(m_contextData);
		new(PlaceHolder(), m_context) YamlContext{};
		m_context->current = m_context->tree.rootref();
		m_context->current |= ryml::MAP;
	}

	YamlArchiveWriter::~YamlArchiveWriter()
	{
		m_context->~YamlContext();
	}

	void YamlArchiveWriter::WriteBool(StringView name, bool value)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current.append_child() << ryml::key(key) = value ? "true" : "false";
	}

	void YamlArchiveWriter::WriteInt(StringView name, i64 value)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current.append_child() << ryml::key(key) << value;
	}

	void YamlArchiveWriter::WriteUInt(StringView name, u64 value)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current.append_child() << ryml::key(key) << value;
	}

	void YamlArchiveWriter::WriteFloat(StringView name, f64 value)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current.append_child() << ryml::key(key) << value;
	}

	void YamlArchiveWriter::WriteString(StringView name, StringView value)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current.append_child() << ryml::key(key) << c4::csubstr(value.begin(), value.end());
	}

	void YamlArchiveWriter::WriteBlob(StringView name, ConstPtr data, u64 size)
	{
		SK_ASSERT(false, "blob is not available for yaml serialization");
	}

	void YamlArchiveWriter::AddBool(bool value)
	{
		m_context->current.append_child() = value ? "true" : "false";
	}

	void YamlArchiveWriter::AddInt(i64 value)
	{
		m_context->current.append_child() << value;
	}

	void YamlArchiveWriter::AddUInt(u64 value)
	{
		m_context->current.append_child() << value;
	}

	void YamlArchiveWriter::AddFloat(f64 value)
	{
		m_context->current.append_child() << value;
	}

	void YamlArchiveWriter::AddString(StringView value)
	{
		m_context->current.append_child() << c4::csubstr(value.begin(), value.end());
	}

	void YamlArchiveWriter::AddBlob(ConstPtr data, u64 size)
	{
		SK_ASSERT(false, "blob is not available for yaml serialization");
	}

	void YamlArchiveWriter::BeginSeq()
	{
		m_context->current = m_context->current.append_child();
		m_context->current |= ryml::SEQ;
	}

	void YamlArchiveWriter::BeginSeq(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current = m_context->current.append_child() << ryml::key(key);
		m_context->current |= ryml::SEQ;
	}

	void YamlArchiveWriter::EndSeq()
	{
		m_context->current = m_context->current.parent();
	}

	void YamlArchiveWriter::BeginMap()
	{
		m_context->current = m_context->current.append_child();
		m_context->current |= ryml::MAP;
	}

	void YamlArchiveWriter::BeginMap(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto key = c4::csubstr(name.begin(), name.end());
		m_context->current = m_context->current.append_child() << ryml::key(key);
		m_context->current |= ryml::MAP;
	}

	void YamlArchiveWriter::EndMap()
	{
		m_context->current = m_context->current.parent();
	}

	String YamlArchiveWriter::EmitAsString() const
	{
		std::string str_result = ryml::emitrs_yaml<std::string>(m_context->tree);
		return {str_result.c_str(), str_result.size()};
	}

	struct ContextState
	{
		ryml::ConstNodeRef                          node;
		std::optional<ryml::ConstNodeRef::iterator> iter;
	};

	struct YamlArchiveReader::YamlContext
	{
		ryml::Tree          tree;
		Array<ContextState> stateStack;

		YamlContext() : stateStack(4) // Start with capacity for 4 nested levels
		{}

		~YamlContext()
		{
			stateStack.Clear();
		}

		ryml::ConstNodeRef GetCurrentNode() const
		{
			if (stateStack.Empty())
			{
				return tree.rootref();
			}
			return stateStack.Back().node;
		}

		std::optional<ryml::ConstNodeRef::iterator>& GetCurrentIter()
		{
			SK_ASSERT(!stateStack.Empty(), "State stack is empty");
			return stateStack.Back().iter;
		}

		ryml::ConstNodeRef GetCurrentItem() const
		{
			if (stateStack.Empty() || !stateStack.Back().iter.has_value())
			{
				return {};
			}
			return *stateStack.Back().iter.value();
		}

		// Check if a node exists with the given name
		ryml::ConstNodeRef FindChildByName(StringView name) const
		{
			c4::csubstr key = c4::csubstr(name.begin(), name.end());

			ryml::ConstNodeRef currentNode = GetCurrentNode();
			if (currentNode.is_map())
			{
				return currentNode.find_child(key);
			}
			return {};
		}

		void PushState(const ryml::ConstNodeRef& node)
		{
			ContextState state;
			state.node = node;
			stateStack.PushBack(state);
		}

		void PopState()
		{
			SK_ASSERT(!stateStack.Empty(), "Cannot pop from empty state stack");
			stateStack.PopBack();
		}
	};


	YamlArchiveReader::YamlArchiveReader(StringView yaml)
	{
		static_assert(sizeof(YamlContext) <= sizeof(m_contextData), "YamlContext is smaller than context buffer ");
		m_context = reinterpret_cast<YamlContext*>(m_contextData);
		new(PlaceHolder(), m_context) YamlContext{};

		m_context->tree = ryml::parse_in_arena(c4::csubstr(yaml.begin(), yaml.end()));
		// Initialize with root node
		m_context->PushState(m_context->tree.rootref());
	}

	bool YamlArchiveReader::ReadBool(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		ryml::ConstNodeRef child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return {};
		}

		c4::csubstr val = child.val();
		return val == "true" || val == "yes" || val == "1";
	}

	i64 YamlArchiveReader::ReadInt(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		auto child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return {};
		}

		i64 result;
		child >> result;
		return result;
	}

	u64 YamlArchiveReader::ReadUInt(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		ryml::ConstNodeRef child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return {};
		}

		u64 result;
		child >> result;
		return result;
	}

	f64 YamlArchiveReader::ReadFloat(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		ryml::ConstNodeRef child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return {};
		}
		f64 result;
		child >> result;
		return result;
	}

	StringView YamlArchiveReader::ReadString(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		ryml::ConstNodeRef child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return {};
		}
		c4::csubstr val = child.val();
		return StringView(val.str, val.len);
	}

	bool YamlArchiveReader::GetBool()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		if (node.invalid())
		{
			return false;
		}
		c4::csubstr val = node.val();
		return val == "true" || val == "yes" || val == "1";
	}

	i64 YamlArchiveReader::GetInt()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		if (node.invalid())
		{
			return 0;
		}

		i64 result;
		node >> result;
		return result;
	}

	u64 YamlArchiveReader::GetUInt()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		if (node.invalid())
		{
			return 0;
		}
		u64 result;
		node >> result;
		return result;
	}

	f64 YamlArchiveReader::GetFloat()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		if (node.invalid())
		{
			return 0.0;
		}

		f64 result;
		node >> result;
		return result;
	}

	StringView YamlArchiveReader::GetString()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		if (node.invalid())
		{
			return "";
		}

		c4::csubstr val = node.val();
		return StringView(val.str, val.len);
	}

	Span<u8> YamlArchiveReader::GetBlob()
	{
		SK_ASSERT(false, "blob is not available for yaml serialization");
		return {};
	}

	void YamlArchiveReader::BeginSeq()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		SK_ASSERT(node.valid(), "Current item is invalid");
		SK_ASSERT(node.is_seq(), "Current item is not a sequence");

		m_context->PushState(node);
	}

	bool YamlArchiveReader::BeginSeq(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		ryml::ConstNodeRef child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return false;
		}
		SK_ASSERT(child.is_seq(), "Node is not a sequence");
		m_context->PushState(child);
		return true;
	}

	bool YamlArchiveReader::NextSeqEntry()
	{
		auto& currentIter = m_context->GetCurrentIter();
		auto currentNode = m_context->GetCurrentNode();

		if (currentNode.begin() == currentNode.end())
		{
			return false;
		}

		if (!currentIter.has_value())
		{
			currentIter = currentNode.begin();
			return true;
		}

		++*currentIter;
		return *currentIter != m_context->GetCurrentNode().end();
	}

	void YamlArchiveReader::EndSeq()
	{
		m_context->PopState();
	}

	void YamlArchiveReader::BeginMap()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		SK_ASSERT(node.valid(), "Current item is invalid");
		SK_ASSERT(node.is_map(), "Current item is not a map");

		m_context->PushState(node);
	}

	bool YamlArchiveReader::BeginMap(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		ryml::ConstNodeRef child = m_context->FindChildByName(name);
		if (child.invalid())
		{
			return false;
		}
		SK_ASSERT(child.is_map(), "Node is not a map");
		m_context->PushState(child);
		return true;
	}

	bool YamlArchiveReader::NextMapEntry()
	{
		auto& currentIter = m_context->GetCurrentIter();

		if (!currentIter.has_value())
		{
			currentIter = m_context->GetCurrentNode().begin();
			return currentIter != m_context->GetCurrentNode().end();
		}

		++(*currentIter);
		return *currentIter != m_context->GetCurrentNode().end();
	}

	StringView YamlArchiveReader::GetCurrentKey()
	{
		ryml::ConstNodeRef node = m_context->GetCurrentItem();
		if (node.invalid())
		{
			return "";
		}

		c4::csubstr key = node.key();
		return StringView(key.str, key.len);
	}

	void YamlArchiveReader::EndMap()
	{
		m_context->PopState();
	}


	JsonArchiveWriter::JsonArchiveWriter()
	{
		m_doc = yyjson_mut_doc_new(&alloc);
		m_current = yyjson_mut_obj(static_cast<yyjson_mut_doc*>(m_doc));
	}

	JsonArchiveWriter::~JsonArchiveWriter()
	{
		yyjson_mut_doc_free(static_cast<yyjson_mut_doc*>(m_doc));
	}

	void JsonArchiveWriter::WriteBool(StringView name, bool value)
	{
		yyjson_mut_obj_add_bool(static_cast<yyjson_mut_doc*>(m_doc), static_cast<yyjson_mut_val*>(m_current), name.CStr(), value);
	}
	void JsonArchiveWriter::WriteInt(StringView name, i64 value)
	{

	}

	void JsonArchiveWriter::WriteUInt(StringView name, u64 value) {}
	void JsonArchiveWriter::WriteFloat(StringView name, f64 value) {}
	void JsonArchiveWriter::WriteString(StringView name, StringView value) {}
	void JsonArchiveWriter::WriteBlob(StringView name, ConstPtr data, u64 size) {}
	void JsonArchiveWriter::AddBool(bool value) {}
	void JsonArchiveWriter::AddInt(i64 value) {}
	void JsonArchiveWriter::AddUInt(u64 value) {}
	void JsonArchiveWriter::AddFloat(f64 value) {}
	void JsonArchiveWriter::AddString(StringView value) {}
	void JsonArchiveWriter::AddBlob(ConstPtr data, u64 size) {}
	void JsonArchiveWriter::BeginMap() {}
	void JsonArchiveWriter::BeginMap(StringView name) {}
	void JsonArchiveWriter::EndMap() {}
	void JsonArchiveWriter::BeginSeq() {}
	void JsonArchiveWriter::BeginSeq(StringView name) {}
	void JsonArchiveWriter::EndSeq() {}

	String JsonArchiveWriter::EmitAsString() const
	{
		return "";
	}

	BinaryArchiveWriter::BinaryArchiveWriter()
	{
		m_data.Reserve(1000);
	}

	BinaryArchiveWriter::~BinaryArchiveWriter() {}

	void BinaryArchiveWriter::WriteBool(StringView name, bool value)
	{
		WriteMapData(name, &value, sizeof(bool));
	}

	void BinaryArchiveWriter::WriteInt(StringView name, i64 value)
	{
		WriteMapData(name, &value, sizeof(i64));
	}

	void BinaryArchiveWriter::WriteUInt(StringView name, u64 value)
	{
		WriteMapData(name, &value, sizeof(u64));
	}

	void BinaryArchiveWriter::WriteFloat(StringView name, f64 value)
	{
		WriteMapData(name, &value, sizeof(f64));
	}

	void BinaryArchiveWriter::WriteString(StringView name, StringView value)
	{
		WriteMapData(name, value.Data(), value.Size());
	}

	void BinaryArchiveWriter::WriteBlob(StringView name, ConstPtr data, u64 size)
	{
		WriteMapData(name, data, size);
	}

	void BinaryArchiveWriter::AddBool(bool value)
	{
		WriteValue(&value, sizeof(bool));
	}

	void BinaryArchiveWriter::AddInt(i64 value)
	{
		WriteValue(&value, sizeof(i64));
	}

	void BinaryArchiveWriter::AddUInt(u64 value)
	{
		WriteValue(&value, sizeof(u64));
	}

	void BinaryArchiveWriter::AddFloat(f64 value)
	{
		WriteValue(&value, sizeof(f64));
	}

	void BinaryArchiveWriter::AddString(StringView value)
	{
		WriteValue(value.CStr(), value.Size());
	}

	void BinaryArchiveWriter::AddBlob(ConstPtr data, u64 size)
	{
		WriteValue(data, size);
	}

	void BinaryArchiveWriter::BeginSeq()
	{
		m_stack.EmplaceBack(m_data.Size());

		const u64 size = 0;
		m_data.Append(reinterpret_cast<const u8*>(&size), sizeof(u64));
	}

	void BinaryArchiveWriter::BeginSeq(StringView name)
	{
		BeginMap(name);
	}

	void BinaryArchiveWriter::EndSeq()
	{
		EndMap();
	}

	void BinaryArchiveWriter::BeginMap()
	{
		m_stack.EmplaceBack(m_data.Size());

		const u64 size = 0;
		m_data.Append(reinterpret_cast<const u8*>(&size), sizeof(u64));
	}

	void BinaryArchiveWriter::BeginMap(StringView name)
	{
		WriteName(name);
		m_stack.EmplaceBack(m_data.Size());

		const u64 size = 0;
		m_data.Append(reinterpret_cast<const u8*>(&size), sizeof(u64));
	}

	void BinaryArchiveWriter::EndMap()
	{
		BinaryStack& currentStack = m_stack.Back();

		const u64 size = m_data.Size() - currentStack.initData - sizeof(u64);
		memcpy(m_data.Data() + currentStack.initData, &size, sizeof(u64));
		PopStack();
	}

	Span<u8> BinaryArchiveWriter::GetData() const
	{
		return {m_data};
	}

	void BinaryArchiveWriter::WriteMapData(StringView name, ConstPtr value, u64 size)
	{
		WriteName(name);
		WriteValue(value, size);
	}

	void BinaryArchiveWriter::WriteName(StringView name)
	{
		u32 nameSize = name.Size();
		m_data.Append(reinterpret_cast<u8*>(&nameSize), sizeof(u32));
		m_data.Append(reinterpret_cast<const u8*>(name.CStr()), nameSize);
	}

	void BinaryArchiveWriter::WriteValue(ConstPtr value, u64 size)
	{
		m_data.Append(reinterpret_cast<u8*>(&size), sizeof(u64));
		m_data.Append(static_cast<const u8*>(value), size);
	}

	void BinaryArchiveWriter::PopStack()
	{
		m_stack.PopBack();
	}

	BinaryArchiveReader::BinaryArchiveReader(Span<u8> data) : m_data(data)
	{
		m_stack.EmplaceBack(BinaryStack{0, m_data.Size(), U64_MAX, IterType::Map});
	}

	bool BinaryArchiveReader::ReadBool(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			return *reinterpret_cast<const bool*>(m_data.Data() + data.pos);
		}
		return false;
	}

	i64 BinaryArchiveReader::ReadInt(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			return *reinterpret_cast<const i64*>(m_data.Data() + data.pos);
		}
		return false;
	}

	u64 BinaryArchiveReader::ReadUInt(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			return *reinterpret_cast<const u64*>(m_data.Data() + data.pos);
		}
		return false;
	}

	f64 BinaryArchiveReader::ReadFloat(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			return *reinterpret_cast<const f64*>(m_data.Data() + data.pos);
		}
		return false;
	}

	StringView BinaryArchiveReader::ReadString(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			return {reinterpret_cast<const char*>(m_data.Data() + data.pos), data.size};
		}
		return {};
	}

	bool BinaryArchiveReader::GetBool()
	{
		BinaryStack& stack = m_stack.Back();

		ValueOffset valueOffset = {};
		ReadOffset(valueOffset, stack);
		return *reinterpret_cast<const bool*>(m_data.Data() + valueOffset.pos);
	}

	i64 BinaryArchiveReader::GetInt()
	{
		BinaryStack& stack = m_stack.Back();
		ValueOffset  valueOffset = {};
		ReadOffset(valueOffset, stack);
		return *reinterpret_cast<const i64*>(m_data.Data() + valueOffset.pos);
	}

	u64 BinaryArchiveReader::GetUInt()
	{
		BinaryStack& stack = m_stack.Back();
		ValueOffset  valueOffset = {};
		ReadOffset(valueOffset, stack);
		return *reinterpret_cast<const u64*>(m_data.Data() + valueOffset.pos);
	}

	f64 BinaryArchiveReader::GetFloat()
	{
		BinaryStack& stack = m_stack.Back();
		ValueOffset  valueOffset = {};
		ReadOffset(valueOffset, stack);
		return *reinterpret_cast<const f64*>(m_data.Data() + valueOffset.pos);
	}

	StringView BinaryArchiveReader::GetString()
	{
		BinaryStack& stack = m_stack.Back();
		ValueOffset  valueOffset = {};
		ReadOffset(valueOffset, stack);
		return {reinterpret_cast<const char*>(m_data.Data() + valueOffset.pos), valueOffset.size};
	}

	Span<u8> BinaryArchiveReader::GetBlob()
	{
		BinaryStack& stack = m_stack.Back();
		ValueOffset  valueOffset = {};
		ReadOffset(valueOffset, stack);
		return Span{const_cast<u8*>(m_data.Data() + valueOffset.pos), valueOffset.size};
	}

	void BinaryArchiveReader::BeginSeq()
	{
		BinaryStack& stack = m_stack.Back();

		ValueOffset field = {};
		ReadOffset(field, stack);
		m_stack.EmplaceBack(field.pos, field.pos + field.size, U64_MAX, IterType::Seq);
	}

	bool BinaryArchiveReader::BeginSeq(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			m_stack.EmplaceBack(data.pos, data.pos + data.size, U64_MAX, IterType::Seq);
			return true;
		}
		return false;
	}

	bool BinaryArchiveReader::NextSeqEntry()
	{
		BinaryStack& stack = m_stack.Back();

		if (stack.end == 0)
		{
			return false;
		}

		if (stack.iter == U64_MAX)
		{
			stack.iter = stack.begin;
		}
		else
		{
			ValueOffset valueOffset = {};
			ReadOffset(valueOffset, stack);
			stack.iter += sizeof(u64) + valueOffset.size;
		}
		return stack.iter < stack.end;
	}

	void BinaryArchiveReader::EndSeq()
	{
		EndMap();
	}

	void BinaryArchiveReader::BeginMap()
	{
		BinaryStack& stack = m_stack.Back();

		ValueOffset field = {};
		ReadOffset(field, stack);
		m_stack.EmplaceBack(field.pos, field.pos + field.size, U64_MAX, IterType::Map);
	}

	bool BinaryArchiveReader::BeginMap(StringView name)
	{
		MapField data;
		if (ReadMapField(data, name))
		{
			m_stack.EmplaceBack(data.pos, data.pos + data.size, U64_MAX, IterType::Map);
			return true;
		}
		return false;
	}

	bool BinaryArchiveReader::NextMapEntry()
	{
		BinaryStack& stack = m_stack.Back();
		if (stack.iter == U64_MAX)
		{
			stack.iter = stack.begin;
		}
		else
		{
			MapField field = {};
			ReadMapField(field, stack.iter);
			stack.iter += sizeof(u32) + field.name.Size() + sizeof(u64) + field.size;
		}
		return stack.iter < stack.end;
	}

	StringView BinaryArchiveReader::GetCurrentKey()
	{
		BinaryStack& stack = m_stack.Back();
		if (stack.iter == U64_MAX || stack.iter > stack.end)
		{
			return {};
		}
		MapField data;
		ReadMapField(data, stack.iter);
		return data.name;
	}

	void BinaryArchiveReader::EndMap()
	{
		m_stack.PopBack();
	}

	void BinaryArchiveReader::ReadMapField(MapField& data, u64 offset) const
	{
		u32 nameSize = *reinterpret_cast<const u32*>(m_data.Data() + offset);

		data.name = StringView{reinterpret_cast<const char*>(m_data.Data() + offset + sizeof(u32)), nameSize};
		data.size = *reinterpret_cast<const u64*>(m_data.Data() + offset + sizeof(u32) + nameSize);
		data.pos = offset + sizeof(u32) + nameSize + sizeof(u64);
	}

	void BinaryArchiveReader::ReadOffset(ValueOffset& data, const BinaryStack& stack) const
	{
		u64 offset = stack.iter;

		if (stack.type == IterType::Seq)
		{
			data.size = *reinterpret_cast<const u64*>(m_data.Data() + offset);
			data.pos = offset + sizeof(u64);
		}
		else if (stack.type == IterType::Map)
		{
			u32 nameSize = *reinterpret_cast<const u32*>(m_data.Data() + offset);
			data.size = *reinterpret_cast<const u64*>(m_data.Data() + offset + sizeof(u32) + nameSize);
			data.pos = offset + sizeof(u32) + nameSize + sizeof(u64);
		}
	}

	bool BinaryArchiveReader::ReadMapField(MapField& data, StringView name) const
	{
		const BinaryStack& currentStack = m_stack.Back();
		u64                current = currentStack.begin;
		while (current < currentStack.end)
		{
			ReadMapField(data, current);
			if (data.name == name)
			{
				return true;
			}
			current += sizeof(u32) + data.name.Size() + sizeof(u64) + data.size;
		}
		return false;
	}

	void Serialization::Serialize(const ReflectType* reflectType, ArchiveWriter& writer, ConstPtr instance)
	{
		if (reflectType == nullptr || instance == nullptr) return;

		for (ReflectField* field : reflectType->GetFields())
		{
			field->Serialize(writer, instance);
		}
	}

	void Serialization::Deserialize(const ReflectType* reflectType, ArchiveReader& reader, VoidPtr instance)
	{
		while (reader.NextMapEntry())
		{
			if (ReflectField* field = reflectType->FindField(reader.GetCurrentKey()))
			{
				field->Deserialize(reader, instance);
			}
		}
	}

	StringView Serialization::GetEnumDesc(const ReflectType* reflectType, ConstPtr value)
	{
		SK_ASSERT(reflectType, "reflectType cannot be null");
		if (ReflectValue* reflectValue = reflectType->FindValue(value))
		{
			return reflectValue->GetDesc();
		}
		return "";
	}

	ConstPtr Serialization::GetEnumValue(const ReflectType* reflectType, StringView desc)
	{
		SK_ASSERT(reflectType, "reflectType cannot be null");
		if (ReflectValue* reflectValue = reflectType->FindValueByName(desc))
		{
			return reflectValue->GetValue();
		}
		return nullptr;
	}

	ReflectType* Serialization::FindTypeToSerialize(TypeID typeId)
	{
		if (ReflectType* type = Reflection::FindTypeById(typeId); type != nullptr && (!type->GetFields().Empty() || !type->GetValues().Empty()))
		{
			return type;
		}
		return nullptr;
	}
}
