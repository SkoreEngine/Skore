#include "Repository.hpp"


namespace Skore
{


    namespace
    {
        bool enabled = false;
    }


    TransactionScope* Repository::CreateTransactionScope()
    {
        return nullptr;
    }

    RID Repository::CreateObjectOf(TypeID typeId, TransactionScope* transactionScope)
    {
        return {};
    }

    RID Repository::CreateObjectFrom(RID rid, TransactionScope* transactionScope)
    {
        return {};
    }

    void Repository::RegisterHandler(RID rid, UUID uuid, ResourceHandler* resourceHandler)
    {

    }

    void Repository::SetPath(RID rid, StringView path)
    {

    }


    RID Repository::GetFromUUID(UUID uuid)
    {
        return {};
    }

    RID Repository::GetFromPath(StringView path)
    {
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
}
