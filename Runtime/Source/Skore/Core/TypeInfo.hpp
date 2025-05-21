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

#include "StringView.hpp"

template <typename Type>
constexpr auto Skore_StrippedTypeName()
{
	Skore::StringView prettyFunction = SK_PRETTY_FUNCTION;
	Skore::usize      first = prettyFunction.FindFirstNotOf(' ', prettyFunction.FindFirstOf(SK_PRETTY_FUNCTION_PREFIX) + 1);
	Skore::StringView value = prettyFunction.Substr(first, prettyFunction.FindLastOf(SK_PRETTY_FUNCTION_SUFFIX) - first);
	return value;
}

template <auto Type>
constexpr auto Skore_StrippedEnumFieldName()
{
	Skore::StringView prettyFunction = SK_PRETTY_FUNCTION;
	Skore::usize      first = prettyFunction.FindFirstNotOf(' ', prettyFunction.FindFirstOf(SK_PRETTY_FUNCTION_PREFIX) + 1);
	Skore::StringView value = prettyFunction.Substr(first, prettyFunction.FindLastOf(SK_PRETTY_FUNCTION_SUFFIX) - first);
	Skore::usize      lastOf = value.FindLastOf(':') + 1;
	return value.Substr(lastOf, value.Size() - lastOf);
}

namespace Skore
{
	typedef void (*FnGetTypeApi)(VoidPtr pointer);

	struct TypeProps
	{
		TypeID       typeId;
		TypeID       typeApi;
		StringView   name;
		FnGetTypeApi getTypeApi;
		u32          size;
		u32          alignment;
		bool         isTriviallyCopyable;
		bool         isEnum;
		SK_PAD(6);
	};

	struct FieldProps : TypeProps
	{
		TypeID ownerId;
		bool   isConst;
		bool   isPointer;
		bool   isReference;
		SK_PAD(5);
	};

	inline FieldProps ToFieldProps(TypeProps typeProps)
	{
		FieldProps props = {};
		memcpy(&props, &typeProps, sizeof(TypeProps));
		return props;
	}


	template <typename Type>
	struct TypeInfoGen
	{
		static constexpr auto GetTypeName()
		{
			StringView typeName = Skore_StrippedTypeName<Type>();

			usize space = typeName.FindFirstOf(' ');
			if (space != StringView::s_npos)
			{
				return typeName.Substr(space + 1);
			}
			return typeName;
		}

		constexpr static TypeID GetTypeID()
		{
			constexpr TypeID typeId = Hash<StringView>::Value(Skore_StrippedTypeName<Type>());
			return typeId;
		}
	};

	template <typename Type>
	struct TypeInfo
	{
		constexpr static TypeID ID()
		{
			return TypeInfoGen<Traits::RemoveAll<Type>>::GetTypeID();
		}

		constexpr static StringView Name()
		{
			return TypeInfoGen<Traits::RemoveAll<Type>>::GetTypeName();
		}

		constexpr static usize Size()
		{
			if constexpr (Traits::IsComplete<Traits::RemoveAll<Type>>)
			{
				return sizeof(Traits::RemoveAll<Type>);
			}
			return 0;
		}

		constexpr static usize Align()
		{
			if constexpr (Traits::IsComplete<Traits::RemoveAll<Type>>)
			{
				return alignof(Traits::RemoveAll<Type>);
			}
			return 0;
		}

		constexpr static void MakeProps(TypeProps& props)
		{
			props.typeId = ID();
			props.name = Name();
			props.size = Size();
			props.alignment = Align();
			props.isTriviallyCopyable = Traits::IsTriviallyCopyable<Type>;
			props.isEnum = Traits::IsEnum<Type>;
			props.typeApi = TypeApi<Traits::RemoveAll<Type>>::GetApiId();
			props.getTypeApi = TypeApi<Traits::RemoveAll<Type>>::GetApi;
		}

		constexpr static TypeProps GetProps()
		{
			TypeProps typeProps = {};
			MakeProps(typeProps);
			return typeProps;
		}
	};

	constexpr StringView MakeSimpleName(const StringView name)
	{
		StringView ret{};
		Split(name, StringView{"::"}, [&](StringView str)
		{
			ret = str;
		});
		return ret;
	}


	template <typename Owner, typename Field>
	constexpr void MakeFieldProps(FieldProps& props)
	{
		TypeInfo<Field>::MakeProps(props);
		props.ownerId = TypeInfo<Owner>::ID();
		props.isConst = std::is_const_v<Field>;
		props.isPointer = Traits::IsPointer<Field>;
		props.isReference = std::is_reference_v<Field>;
	}

	template <typename Owner, typename Field>
	constexpr FieldProps GetFieldProps()
	{
		FieldProps props{};
		MakeFieldProps<Owner, Field>(props);
		return props;
	}

	template <auto Type>
	constexpr static StringView GetEnumValueName()
	{
		return Skore_StrippedEnumFieldName<Type>();
	}
}
