using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeArray<T> : IDisposable, IEnumerable<T>, IEquatable<NativeArray<T>> where T : unmanaged
    {
        private T*         _first;
        private T*         _last;
        private T*         _capacity;
        private Allocator* _allocator;

        public NativeArray(Allocator* allocator)
        {
            _first = null;
            _last = null;
            _capacity = null;
            _allocator = allocator;
        }

        public NativeArray(int capacity) : this(Allocator.Default)
        {
            if (capacity > 0)
            {
                Reserve(capacity);
            }
        }

        public NativeArray(ReadOnlySpan<T> values) : this(Allocator.Default)
        {
            Reserve(values.Length);
            for (int i = 0; i < values.Length; i++)
            {
                _first[i] = values[i];
            }
            _last = _first + values.Length;
        }

        public bool IsCreated => _first != null;

        public int Count => (int)(_last - _first);

        public int Capacity => (int)(_capacity - _first);

        public bool IsEmpty => _first == _last;

        public T* Data => _first;

        public ref T this[int index]
        {
            get
            {
                if ((uint)index >= (uint)Count)
                {
                    throw new IndexOutOfRangeException();
                }
                return ref _first[index];
            }
        }

        public void Add(T value)
        {
            if (_last == _capacity)
            {
                Reserve(Count == 0 ? 4 : Count * 2);
            }
            *_last++ = value;
        }

        public void Clear()
        {
            _last = _first;
        }

        public void Reserve(int newCapacity)
        {
            if (newCapacity <= Capacity)
            {
                return;
            }
            if (_allocator == null)
            {
                _allocator = Allocator.Default;
            }
            int count = Count;
            T*  newFirst = (T*)_allocator->MemAlloc(_allocator->Context, (ulong)((long)newCapacity * sizeof(T)));
            if (_first != null)
            {
                Buffer.MemoryCopy(_first, newFirst, (long)newCapacity * sizeof(T), (long)count * sizeof(T));
                _allocator->MemFree(_allocator->Context, _first);
            }
            _first = newFirst;
            _last = newFirst + count;
            _capacity = newFirst + newCapacity;
        }

        public T[] ToArray()
        {
            int count = Count;
            T[] result = new T[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = _first[i];
            }
            return result;
        }

        public void Dispose()
        {
            if (_first != null && _allocator != null)
            {
                _allocator->MemFree(_allocator->Context, _first);
            }
            _first = null;
            _last = null;
            _capacity = null;
        }

        public bool Equals(NativeArray<T> other) => _first == other._first && _last == other._last;

        public override bool Equals(object? obj) => obj is NativeArray<T> other && Equals(other);

        public override int GetHashCode() => ((IntPtr)_first).GetHashCode();

        public static bool operator ==(NativeArray<T> a, NativeArray<T> b) => a.Equals(b);

        public static bool operator !=(NativeArray<T> a, NativeArray<T> b) => !a.Equals(b);

        public Enumerator GetEnumerator() => new Enumerator(this);

        IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        public struct Enumerator : IEnumerator<T>
        {
            private readonly NativeArray<T> _array;
            private int                     _index;

            public Enumerator(NativeArray<T> array)
            {
                _array = array;
                _index = -1;
            }

            public bool MoveNext() => ++_index < _array.Count;

            public void Reset() => _index = -1;

            public T Current => _array._first[_index];

            object IEnumerator.Current => Current;

            public void Dispose()
            {
            }
        }
    }
}
