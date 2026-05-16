#pragma once

#include "Skore/Common.hpp"
#include "TypeInfo.hpp"
#include <initializer_list>

namespace Skore
{
    template<typename T>
    class Array;

    template<typename T, usize bufferSize>
    class FixedArray;

    template<typename T>
    class BasicByteBuffer;

    template<typename T>
    class Span
    {
    public:
        typedef T ValueType;
        typedef T* Iterator;
        typedef const T* ConstIterator;

        constexpr Span() : m_first(0), m_last(0)
        {}

        constexpr Span(Array<T>& arr) : m_first(arr.Data()), m_last(arr.Data() + arr.Size())
        {
        }

        constexpr Span(const Array<T>& arr) : m_first((T*) arr.Data()), m_last((T*) arr.Data() + arr.Size())
        {
        }

        template<usize size>
        constexpr Span(const FixedArray<T, size>& vec) : m_first((T*) vec.Data()), m_last((T*) vec.Data() + vec.Size())
        {
        }

        constexpr Span(T* t) : m_first(t), m_last(t + 1)
        {
        }

        constexpr Span(T* first, T* last) : m_first(first), m_last(last)
        {}

        constexpr Span(T* first, usize size) : m_first(first), m_last(first + size)
        {}

        constexpr Span(std::initializer_list<T> initializerList) : m_first((T*) initializerList.begin()), m_last((T*) initializerList.end())
        {
        }

        constexpr Span(const BasicByteBuffer<T>& buffer) : m_first((T*)buffer.begin()), m_last((T*)buffer.end())
        {
        }

        constexpr const T* Data() const
        {
            return begin();
        }

        constexpr Iterator begin()
        {
            return m_first;
        }

        constexpr Iterator end()
        {
            return m_last;
        }

        constexpr ConstIterator begin() const
        {
            return m_first;
        }

        constexpr ConstIterator end() const
        {
            return m_last;
        }

        constexpr usize Size() const
        {
            return (usize) (end() - begin());
        }

        constexpr bool Empty() const
        {
            return Size() == 0;
        }

        constexpr T& operator[](usize idx)
        {
            return begin()[idx];
        }

        constexpr const T& operator[](usize idx) const
        {
            return begin()[idx];
        }

        constexpr const T& Back() const
        {
            return begin()[Size() - 1];
        }

        constexpr T& Back()
        {
            return begin()[Size() - 1];
        }

        constexpr bool operator==(const Span& other) const
        {
            if (this->Size() != other.Size()) return false;
            for (int i = 0; i < this->Size(); ++i)
            {
                if (this->operator[](i) != other[i])
                {
                    return false;
                }
            }
            return true;
        }

        constexpr bool operator!=(const Span& other) const
        {
            return !((*this) == other);
        }

        constexpr usize IndexOf(T object)
        {
            return FindFirstIndex(begin(), end(), object);
        }

    private:
        T* m_first;
        T* m_last;
    };

    template<typename T>
    bool operator==(const Span<T>& l, const Array<T>& r)
    {
        if (l.Size() != r.Size()) return false;

        for (usize i = 0; i < l.Size(); ++i)
        {
            if (l[i] != r[i])
            {
                return false;
            }
        }
        return true;
    }

    struct SpanApi
    {
        usize (*Size)(ConstPtr instance);
        VoidPtr (*Get)(VoidPtr instance, usize index);
        TypeProps (*GetProps)();
    };

    template<typename Type>
    struct TypeApi<Span<Type>>
    {
        static void GetApi(VoidPtr pointer)
        {
            new(PlaceHolder{}, pointer) SpanApi{};
            SpanApi& api = *static_cast<SpanApi*>(pointer);

            api.Size = [](ConstPtr instance)
            {
                return static_cast<const Span<Type>*>(instance)->Size();
            };

            api.Get = [](VoidPtr instance, usize index)
            {
                Span<Type>& span = *static_cast<Span<Type>*>(instance);
                return static_cast<VoidPtr>(&span[index]);
            };

            api.GetProps = []
            {
                return TypeInfo<Type>::GetProps();
            };
        }

        static constexpr TypeID GetApiId()
        {
            return TypeInfo<SpanApi>::ID();
        }
    };
}
