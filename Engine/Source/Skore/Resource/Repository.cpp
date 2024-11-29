#include "Repository.hpp"


namespace Skore
{
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
}
