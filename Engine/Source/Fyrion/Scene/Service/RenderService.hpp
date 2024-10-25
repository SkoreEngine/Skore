#pragma once
#include "Service.hpp"

namespace Fyrion
{
    // maybe make it abstract and implement it in the RenderPipeline??
    class FY_API RenderService final : public Service
    {
    public:
        FY_BASE_TYPES(Service);
        FY_NO_COPY_CONSTRUCTOR(RenderService);

        RID  RegisterItem();
        void RemoveItem(const RID& rid);

        static void RegisterType(NativeTypeHandler<RenderService>& type);

    private:

    };
}
