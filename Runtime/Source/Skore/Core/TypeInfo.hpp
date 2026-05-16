#pragma once

#include "StringView.hpp"

#define sktypeid(Type) TypeInfo<Type>::ID()

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
	typedef void (*FnCopy)(VoidPtr dest, VoidPtr src);
	typedef void (*FnDestroy)(VoidPtr instance);

	struct TypeProps
	{
		TypeID       typeId;
		TypeID       typeApi;
		StringView   name;
		FnGetTypeApi getTypeApi;
		FnCopy       fnCopy;
		FnDestroy    fnDestroy;
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
			constexpr TypeID typeId = TypeID{Hash<StringView>::Value(Skore_StrippedTypeName<Type>())};
			return typeId;
		}
	};

	template <typename Type>
	struct SK_API TypeInfo
	{
		using RealType = Traits::RemoveAll<Type>;

		constexpr static void Copy(VoidPtr dest, VoidPtr src)
		{
			if constexpr (Traits::IsComplete<RealType>)
			{
				if constexpr (std::is_copy_constructible_v<RealType>)
				{
					new(dest) RealType(*static_cast<const RealType*>(src));
				}
			}
		}

		constexpr static void Destroy(VoidPtr instance)
		{
			if constexpr (Traits::IsComplete<RealType>)
			{
				if constexpr (std::is_destructible_v<RealType>)
				{
					static_cast<RealType*>(instance)->~RealType();
				}
			}
		}

		constexpr static TypeID ID()
		{
			return TypeInfoGen<RealType>::GetTypeID();
		}

		constexpr static StringView Name()
		{
			return TypeInfoGen<RealType>::GetTypeName();
		}

		constexpr static usize Size()
		{
			if constexpr (Traits::IsComplete<RealType>)
			{
				return sizeof(RealType);
			}
			return 0;
		}

		constexpr static usize Align()
		{
			if constexpr (Traits::IsComplete<RealType>)
			{
				return alignof(RealType);
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
			props.fnCopy = Copy;
			props.fnDestroy = Destroy;
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
