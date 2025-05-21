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

#include "Event.hpp"
#include "HashMap.hpp"
#include "HashSet.hpp"

namespace Skore
{
    static std::atomic_bool executingEvents = false;


    struct EventFunctionData
    {
        VoidPtr         userData{};
        VoidPtr         instance{};
        FnEventCallback callback{};
    };

    struct EventsToRemove
    {
        TypeID typeId;
        VoidPtr userData;
        VoidPtr instance;
        FnEventCallback eventCallback;
    };

    Array<EventsToRemove> toRemove;

    bool operator==(const EventFunctionData& r, const EventFunctionData& l)
    {
        return reinterpret_cast<usize>(r.instance) == reinterpret_cast<usize>(l.instance)
            && reinterpret_cast<usize>(r.userData) == reinterpret_cast<usize>(l.userData)
            && reinterpret_cast<usize>(r.callback) == reinterpret_cast<usize>(l.callback);
    }

    template<>
    struct Hash<EventFunctionData>
    {
        constexpr static bool hasHash = true;
        static usize Value(const EventFunctionData& functionData)
        {
            return Hash<usize>::Value(reinterpret_cast<usize>(functionData.instance)) << 2 ^ Hash<usize>::Value(reinterpret_cast<usize>(functionData.callback)) >> 2;
        }
    };

    struct EventTypeData
    {
        HashSet<EventFunctionData> events{};
    };


    namespace
    {
        HashMap<TypeID, EventTypeData>& GetEvents()
        {
            static HashMap<TypeID, EventTypeData> events{};
            return events;
        }

    }

    void Event::Bind(TypeID typeId, VoidPtr userData, VoidPtr instance, FnEventCallback eventCallback)
    {
        HashMap<TypeID, EventTypeData>& events = GetEvents();
        auto it = events.Find(typeId);
        if (it == events.end())
        {
            it = events.Emplace(typeId, EventTypeData{}).first;
        }

        it->second.events.Emplace(EventFunctionData{
            .userData = userData,
            .instance = instance,
            .callback = eventCallback
        });
    }

    void Event::Unbind(TypeID typeId, VoidPtr userData, VoidPtr instance, FnEventCallback eventCallback)
    {
        if (executingEvents)
        {
            toRemove.EmplaceBack(typeId, userData, instance, eventCallback);
            return;
        }

        HashMap<TypeID, EventTypeData>& events = GetEvents();

        auto it = events.Find(typeId);
        if (it != events.end())
        {
            it->second.events.Erase(EventFunctionData{
                .userData = userData,
                .instance = instance,
                .callback = eventCallback
            });
        }
    }

    usize Event::EventCount(TypeID typeId)
    {
        HashMap<TypeID, EventTypeData>& events = GetEvents();

        auto it = events.Find(typeId);
        if (it != events.end())
        {
            return it->second.events.Size();
        }
        return 0;
    }

    EventTypeData* Event::GetData(TypeID typeId)
    {
        HashMap<TypeID, EventTypeData>& events = GetEvents();

        auto it = events.Find(typeId);
        if (it == events.end())
        {
            it = events.Emplace(typeId, EventTypeData{}).first;
        }
        return &it->second;
    }

    void Event::InvokeEvents(EventTypeData* eventTypeData, VoidPtr* parameters)
    {
        executingEvents = true;
        for(auto& it: eventTypeData->events)
        {
            it.callback(it.userData, it.instance, parameters);
        }
        executingEvents = false;

        for(auto& it: toRemove)
        {
            Unbind(it.typeId, it.userData, it.instance, it.eventCallback);
        }
        toRemove.Clear();
    }

    void EventShutdown()
    {
        HashMap<TypeID, EventTypeData>& events = GetEvents();

        for(auto it: events)
        {
            it.second.events.Clear();
        }
    }

    void Event::Reset()
    {
        EventShutdown();

        HashMap<TypeID, EventTypeData>& events = GetEvents();
        events.Clear();
    }
}
