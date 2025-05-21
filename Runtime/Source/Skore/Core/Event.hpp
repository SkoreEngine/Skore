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

#include "Skore/Common.hpp"
#include "Traits.hpp"
#include "FixedArray.hpp"

namespace Skore
{
	struct EventTypeData;
	typedef void (*FnEventCallback)(VoidPtr userData, VoidPtr instance, VoidPtr* parameters);

	template <usize Id, typename T>
	struct EventType
	{
		static_assert(Traits::AlwaysFalse<T>, "invalid function parameters");
	};

	template <usize Id, typename... Args>
	struct EventType<Id, void(Args...)>
	{
		constexpr static usize id = Id;
	};

	template <auto Func, typename FuncType, typename T>
	class EventInvoker
	{
		static_assert(Traits::AlwaysFalse<T>, "invalid function parameters");
	};

	struct SK_API Event
	{
		static void           Bind(TypeID typeId, VoidPtr userData, VoidPtr instance, FnEventCallback eventCallback);
		static void           Unbind(TypeID typeId, VoidPtr userData, VoidPtr instance, FnEventCallback eventCallback);
		static usize          EventCount(TypeID typeId);
		static EventTypeData* GetData(TypeID typeId);
		static void           InvokeEvents(EventTypeData* eventTypeData, VoidPtr* parameters);

		template <typename T, auto Func>
		static void Bind()
		{
			EventInvoker<Func, decltype(Func), T>::Bind(nullptr);
		}

		template <typename T, auto Func>
		static void Bind(VoidPtr instance)
		{
			EventInvoker<Func, decltype(Func), T>::Bind(instance);
		}

		template <typename T, auto Func>
		static void Unbind()
		{
			EventInvoker<Func, decltype(Func), T>::Unbind(nullptr);
		}

		template <typename T, auto Func>
		static void Unbind(VoidPtr instance)
		{
			EventInvoker<Func, decltype(Func), T>::Unbind(instance);
		}

		template <typename T>
		static usize EventCount()
		{
			return EventCount(T::id);
		}

		static void Reset();
	};


	template <auto Func, usize Id, typename... Args>
	class EventInvoker<Func, void(*)(Args...), EventType<Id, void(Args...)>>
	{
	private:
		template <typename... Vals>
		static void Eval(VoidPtr instance, VoidPtr* params, Traits::IndexSequence<>, Vals&&... vals)
		{
			Func(Traits::Forward<Vals>(vals)...);
		}

		template <typename T, typename... Tp, usize I, usize... Is, typename... Vals>
		static void Eval(VoidPtr instance, VoidPtr* params, Traits::IndexSequence<I, Is...> seq, Vals&&... vals)
		{
			return Eval<Tp...>(instance, params, Traits::IndexSequence<Is...>(), Traits::Forward<Vals>(vals)..., *static_cast<Traits::RemoveReference<T>*>(params[I]));
		}

		static void EventCallback(VoidPtr userData, VoidPtr instance, VoidPtr* parameters)
		{
			const usize size = sizeof...(Args);
			Eval<Args...>(instance, parameters, Traits::MakeIntegerSequence<usize, size>{});
		}

	public:
		static void Bind(VoidPtr instance)
		{
			Event::Bind(Id, nullptr, instance, &EventCallback);
		}

		static void Unbind(VoidPtr instance)
		{
			Event::Unbind(Id, nullptr, instance, &EventCallback);
		}
	};

	template <auto Func, usize Id, typename Owner, typename... Args>
	class EventInvoker<Func, void(Owner::*)(Args...), EventType<Id, void(Args...)>>
	{
	private:
		template <typename... Vals>
		static void Eval(VoidPtr instance, VoidPtr* params, Traits::IndexSequence<>, Vals&&... vals)
		{
			((*static_cast<Owner*>(instance)).*Func)(Traits::Forward<Vals>(vals)...);
		}

		template <typename T, typename... Tp, usize I, usize... Is, typename... Vals>
		static void Eval(VoidPtr instance, VoidPtr* params, Traits::IndexSequence<I, Is...> seq, Vals&&... vals)
		{
			return Eval<Tp...>(instance, params, Traits::IndexSequence<Is...>(), Traits::Forward<Vals>(vals)..., *static_cast<Traits::RemoveReference<T>*>(params[I]));
		}

		static void EventCallback(VoidPtr userData, VoidPtr instance, VoidPtr* parameters)
		{
			const usize size = sizeof...(Args);
			Eval<Args...>(instance, parameters, Traits::MakeIntegerSequence<usize, size>{});
		}

	public:
		static void Bind(VoidPtr instance)
		{
			Event::Bind(Id, nullptr, instance, &EventCallback);
		}

		static void Unbind(VoidPtr instance)
		{
			Event::Unbind(Id, nullptr, instance, &EventCallback);
		}
	};

	template <auto Func, usize Id, typename Owner, typename... Args>
	class EventInvoker<Func, void(Owner::*)(Args...) const, EventType<Id, void(Args...)>>
	{
		template <typename... Vals>
		SK_FINLINE static void Eval(VoidPtr instance, VoidPtr* params, Traits::IndexSequence<>, Vals&&... vals)
		{
			(*static_cast<const Owner*>(instance).*Func)(Traits::Forward<Vals>(vals)...);
		}

		template <typename T, typename... Tp, usize I, usize... Is, typename... Vals>
		SK_FINLINE static void Eval(VoidPtr instance, VoidPtr* params, Traits::IndexSequence<I, Is...> seq, Vals&&... vals)
		{
			return Eval<Tp...>(instance, params, Traits::IndexSequence<Is...>(), Traits::Forward<Vals>(vals)..., *static_cast<T*>(params[I]));
		}

		static void EventCallback(VoidPtr userData, VoidPtr instance, VoidPtr* parameters)
		{
			const usize size = sizeof...(Args);
			Eval<Args...>(instance, parameters, Traits::MakeIntegerSequence<usize, size>{});
		}

	public:
		static void Bind(VoidPtr instance)
		{
			Event::Bind(Id, nullptr, instance, &EventCallback);
		}

		static void Unbind(VoidPtr instance)
		{
			Event::Unbind(Id, nullptr, instance, &EventCallback);
		}
	};

	template <typename T>
	class EventHandler
	{
		static_assert(Traits::AlwaysFalse<T>, "invalid event type");
	};


	template <usize Id, typename... Args>
	class EventHandler<EventType<Id, void(Args...)>>
	{
	public:
		EventHandler() : m_eventTypeData(Event::GetData(Id)) {}

		void Invoke(Args... args)
		{
			FixedArray<VoidPtr, sizeof...(Args)> params = {(VoidPtr)&args...};
			Event::InvokeEvents(m_eventTypeData, params.begin());
		}

	private:
		EventTypeData* m_eventTypeData;
	};
}
