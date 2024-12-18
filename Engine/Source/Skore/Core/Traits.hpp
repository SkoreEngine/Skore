#pragma once

#include "Skore/Common.hpp"

#include <type_traits>
#include <utility>

namespace Skore::Traits
{
    template<auto Value>
    struct ConstantType
    {
        static constexpr decltype(Value) value = Value;
    };

    using TrueType = ConstantType<true>;
    using FalseType = ConstantType<false>;

    template<typename... Types>
    using Void = void;

    template<typename T>
    struct TRemoveReference
    {
        using type = T;
    };

    template<typename T>
    struct TRemoveReference<T&>
    {
        using type = T;
    };

    template<typename T>
    struct TRemoveReference<T&&>
    {
        using type = T;
    };

    template<typename T>
    using RemoveReference = typename TRemoveReference<T>::type;

    template<typename Type>
    struct TRemoveAll
    {
        using type = Type;
    };

    template<typename Type>
    struct TRemoveAll<const Type>
    {
        using type = typename TRemoveAll<Type>::type;
    };

    template<typename Type>
    struct TRemoveAll<Type*>
    {
        using type = typename TRemoveAll<Type>::type;
    };

    template<typename Type>
    struct TRemoveAll<Type&>
    {
        using type = typename TRemoveAll<Type>::type;
    };

    template<typename Type>
    struct TRemoveAll<Type&&>
    {
        using type = typename TRemoveAll<Type>::type;
    };

    template<typename Type>
    using RemoveAll = typename TRemoveAll<Type>::type;


    template<typename Type>
    struct TRemoveConstRef
    {
        using type = Type;
    };


    template<typename Type>
    struct TRemoveConstRef<const Type>
    {
        using type = typename TRemoveConstRef<Type>::type;
    };


    template<typename Type>
    struct TRemoveConstRef<Type&>
    {
        using type = typename TRemoveConstRef<Type>::type;
    };

    template<typename Type>
    struct TRemoveConstRef<Type&&>
    {
        using type = typename TRemoveConstRef<Type>::type;
    };

    template<typename Type>
    using RemoveConstRef = typename TRemoveConstRef<Type>::type;


    template<typename T>
    constexpr T&& Forward(RemoveReference<T>& arg)
    {
        return static_cast<T&&>(arg);
    }

    template<typename T>
    constexpr T&& Forward(RemoveReference<T>&& arg)
    {
        return static_cast<T&&>(arg);
    }

    template<typename T>
    constexpr RemoveReference<T>&& Move(T&& arg)
    {
        return static_cast<RemoveReference<T>&&>(arg);
    }

    template<typename T>
    constexpr bool IsAggregate = __is_aggregate(T);

    typedef decltype(nullptr) NullPtr;

    template<typename T>
    constexpr bool AlwaysFalse = false;

    template<typename T>
    constexpr bool IsIntegral = std::is_integral_v<T>;

    template<class T, T... Vals>
    using IntegerSequence = std::integer_sequence<T, Vals...>;

    template<Skore::usize... Vals>
    using IndexSequence = IntegerSequence<Skore::usize, Vals...>;

    template<class T, T Size>
    using MakeIntegerSequence = std::make_integer_sequence<T, Size>;

    template<class T>
    constexpr bool IsEnum = std::is_enum_v<T>;

    template<class T, class = void>
    struct AddReference
    { // add reference (non-referenceable type)
        using Lvalue = T;
        using Rvalue = T;
    };

    template<class T>
    struct AddReference<T, Void<T&>>
    { // (referenceable type)
        using Lvalue = T&;
        using Rvalue = T&&;
    };

    template<class T>
    using AddRValueReference = typename AddReference<T>::Rvalue;

    template<typename T>
    AddRValueReference<T> Declval() noexcept
    {
        static_assert(AlwaysFalse<T>, "Invalid");
    }

    template<typename Type, typename ... Arguments>
    struct IsDirectConstructibleImpl
    {
        template<typename U, decltype(U{Declval<Arguments>()...})* = nullptr>
        static char Test(int);

        template<typename U>
        static long Test(...);

        static constexpr bool value = sizeof(Test<Type>(0)) == sizeof(char);
    };

    template<typename Type, typename ... Arguments>
    constexpr bool IsDirectConstructible = IsDirectConstructibleImpl<Type, Arguments...>::value;

    template<class T>
    constexpr bool IsDestructible = std::is_destructible_v<T>;

    template<typename... Ts>
    struct MakeVoid
    {
        typedef void type;
    };
    template<typename... Ts> using VoidType = typename MakeVoid<Ts...>::type;

    template<typename Function>
    struct RemoveConstFuncImpl
    {
        using Type = Function;
    };

    template<typename Return, typename Owner, typename ...Args>
    struct RemoveConstFuncImpl<Return(Owner::*)(Args...) const>
    {
        using Type = Return(Owner::*)(Args...);
    };

    template<typename Function>
    using RemoveConstFunc = typename RemoveConstFuncImpl<Function>::Type;

    template<typename T1, typename T2>
    constexpr bool IsSame = std::is_same_v<T1, T2>;

    template<typename T, typename Enabler = void>
    struct IsCompleteImpl : std::false_type
    {
    };

    template<typename T>
    struct IsCompleteImpl<T, Traits::VoidType<decltype(sizeof(T) != 0)>> : std::true_type
    {
    };
    template<typename Type>
    constexpr bool IsComplete = IsCompleteImpl<Type>::value;

    template<typename Type>
    constexpr bool IsMoveConstructible = std::is_move_constructible_v<Type>;

    template<typename Type>
    constexpr bool IsCopyConstructible = std::is_copy_constructible_v<Type>;

    template<bool B, class T = void>
    struct EnableIfImpl
    {
    };

    template<class T>
    struct EnableIfImpl<true, T>
    {
        typedef T Type;
    };

    template<bool B, class T = void>
    using EnableIf = typename EnableIfImpl<B, T>::Type;

    template<typename T, typename Enable = void>
    struct IsTriviallyCopyableImpl
    {
        static constexpr bool value = std::is_trivially_copyable_v<T>;
    };

    template<typename T>
    struct IsTriviallyCopyableImpl<T, EnableIf<!IsComplete<T>>>
    {
        static constexpr bool value = false;
    };

    template<typename Type>
    constexpr bool IsTriviallyCopyable = IsTriviallyCopyableImpl<Type>::value;

    template<typename Base, typename Derived>
    constexpr bool IsBaseOf = std::is_base_of_v<Base, Derived>;


    template<auto func, typename Type>
    struct FuncInfoImpl
    {
        static_assert(Traits::AlwaysFalse<Type>, "info not found");
    };

    template<auto func, typename Return, typename Owner, typename ...Args>
    struct FuncInfoImpl<func, Return (Owner::*)(Args...) const>
    {
        using returnType = Return;
    };

    template<auto func, typename Return, typename Owner, typename ...Args>
    struct FuncInfoImpl<func, Return (Owner::*)(Args...)>
    {
        using returnType = Return;
    };

    template<auto func>
    using FunctionReturnType = typename FuncInfoImpl<func, decltype(func)>::returnType;

    template <typename T, typename U>
    usize OffsetOf(U T::* member)
    {
        return (char*)&((T*)nullptr->*member) - (char*)nullptr;
    }

    template<typename T>
    struct TIsPointer : std::false_type {};

    template<typename T>
    struct TIsPointer<T*> : std::true_type {};

    template<typename Type>
    constexpr bool IsPointer = TIsPointer<Type>::value;

    template <class T, class Tuple>
    struct TupleIndex;

    template <class T, class... Types>
    struct TupleIndex<T, std::tuple<T, Types...>> {
        static constexpr std::size_t value = 0;
    };

    template <class T, class U, class... Types>
    struct TupleIndex<T, std::tuple<U, Types...>> {
        static constexpr std::size_t value = 1 + TupleIndex<T, std::tuple<Types...>>::value;
    };


}