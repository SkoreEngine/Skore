#include "Repository.hpp"

#include <mutex>

#include "Skore/Core/Registry.hpp"


#define PAGE(value)    u32((value)/SK_REPO_PAGE_SIZE)
#define OFFSET(value)  (u32)((value) & (SK_REPO_PAGE_SIZE - 1))


namespace Skore
{
    namespace
    {
        bool enabled = false;


        struct ResourceStorage
        {
            RID                        rid;
            UUID                       uuid;
            String                     path;
            std::atomic<ResourceData*> data;
            TypeHandler*               typeHandler = nullptr;
            ResourceHandler*           resourceHandler = nullptr;
        };

        struct ResourcePage
        {
            ResourceStorage elements[SK_REPO_PAGE_SIZE];
        };

        Allocator& allocator = MemoryGlobals::GetDefaultAllocator();

        std::atomic_size_t counter{};
        usize              pageCount{};
        ResourcePage*      pages[SK_REPO_PAGE_SIZE]{};
        std::mutex         pageMutex{};

        std::mutex         byUUIDMutex{};
        HashMap<UUID, RID> byUUID{};

        std::mutex           byPathMutex{};
        HashMap<String, RID> byPath{};

        ResourceStorage* GetOrAllocate(RID rid)
        {
            if (pages[PAGE(rid.id)] == nullptr)
            {
                std::unique_lock lock(pageMutex);
                if (pages[PAGE(rid.id)] == nullptr)
                {
                    pages[PAGE(rid.id)] = static_cast<ResourcePage*>(allocator.MemAlloc(sizeof(ResourcePage), alignof(ResourcePage)));
                    pageCount++;
                }
            }
            return &pages[PAGE(rid.id)]->elements[OFFSET(rid.id)];
        }

        RID GetID()
        {
            return RID{counter++};
        }


        RID GetID(UUID uuid)
        {
            if (uuid)
            {
                std::unique_lock lock(byUUIDMutex);
                if (auto it = byUUID.Find(uuid))
                {
                    return it->second;
                }
            }
            RID rid = GetID();

            if (uuid)
            {
                std::unique_lock lock(byUUIDMutex);
                byUUID.Insert(uuid, rid);
            }
            return rid;
        }
    }


    TransactionScope* Repository::CreateTransactionScope()
    {
        return nullptr;
    }

    RID Repository::CreateObjectOf(TypeID typeId, UUID uuid, TransactionScope* transactionScope)
    {
        RID              rid = GetID(uuid);
        ResourceStorage* resourceStorage = GetOrAllocate(rid);

        new(PlaceHolder(), resourceStorage) ResourceStorage{
            .rid = rid,
            .uuid = uuid,
            .data = {}
        };

        if (TypeHandler* typeHandler = Registry::FindTypeById(typeId))
        {
            resourceStorage->typeHandler = typeHandler;
        }

        return rid;
    }

    RID Repository::CreateObjectFromPrototype(RID rid, TransactionScope* transactionScope)
    {
        return {};
    }

    void Repository::RegisterHandler(RID rid, ResourceHandler* resourceHandler)
    {
        if (rid && resourceHandler)
        {
            ResourceStorage* storage = &pages[PAGE(rid.id)]->elements[OFFSET(rid.id)];
            storage->resourceHandler = resourceHandler;
        }
    }

    void Repository::SetPath(RID rid, StringView path)
    {
        ResourceStorage* storage = &pages[PAGE(rid.id)]->elements[OFFSET(rid.id)];
        {
            std::unique_lock lockByPath(byPathMutex);
            byPath.Erase(storage->path);
            byPath.Insert(path, rid);
        }
        storage->path = path;
    }


    RID Repository::GetFromUUID(UUID uuid)
    {
        std::unique_lock lock(byUUIDMutex);

        if (auto it = byUUID.Find(uuid))
        {
            return it->second;
        }
        return {};
    }

    RID Repository::GetFromPath(StringView path)
    {
        std::unique_lock lock(byPathMutex);

        if (auto it = byPath.Find(path))
        {
            return it->second;
        }
        return {};
    }

    ResourceObject Repository::Write(RID rid)
    {
        return {};
    }

    ResourceObject Repository::Read(RID rid)
    {
        return {};
    }

    VoidPtr Repository::Instantiate(RID rid)
    {
        return nullptr;
    }

    bool Repository::IsNewResourceSystemEnabled()
    {
        return enabled;
    }

    void Repository::NewResourceSystemEnabled()
    {
        enabled = true;
    }

    void RepositoryInit()
    {
        Repository::CreateObjectOf(0, {});
    }


    void RepositoryShutdown()
    {
        for (u64 i = 0; i < counter; ++i)
        {
            ResourceStorage* storage = &pages[PAGE(i)]->elements[OFFSET(i)];
           // DestroyData(storage->data, false);
            storage->~ResourceStorage();
        }

        for (u64 i = 0; i < pageCount; ++i)
        {
            if (!pages[i])
            {
                break;
            }
            allocator.MemFree(pages[i]);
            pages[i] = nullptr;
        }

        counter = 0;
        pageCount = 0;
        // resourceTypes.Clear();
        // resourceTypesByName.Clear();
        byUUID.Clear();
        byPath.Clear();
    }
}
