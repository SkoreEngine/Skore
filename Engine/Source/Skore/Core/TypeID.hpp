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

namespace Skore
{
    template <typename Type>
    struct TypeIDGen
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
    constexpr static TypeID GetTypeID()
    {
        return TypeIDGen<Traits::RemoveAll<Type>>::GetTypeID();
    }

    template <typename Type>
    constexpr static StringView GetTypeName()
    {
        return TypeIDGen<Traits::RemoveAll<Type>>::GetTypeName();
    }
}
