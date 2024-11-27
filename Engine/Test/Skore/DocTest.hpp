#pragma once

#include <doctest.h>
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

template <>
struct doctest::StringMaker<Skore::String>
{
    static String convert(const Skore::String& str)
    {
        return {str.CStr(), static_cast<String::size_type>(str.Size())};
    }
};

template <>
struct doctest::StringMaker<Skore::StringView>
{
    static String convert(const Skore::StringView& str)
    {
        return {str.CStr(), static_cast<String::size_type>(str.Size())};
    }
};
