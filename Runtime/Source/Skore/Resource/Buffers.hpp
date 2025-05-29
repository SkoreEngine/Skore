#pragma once

#include <memory>

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
    class SK_API BufferInstance
    {
    public:
        virtual ~BufferInstance() = default;
        friend class ResourceBuffer;

    protected:
        virtual u64  GetSize() = 0;
        virtual void GetData(VoidPtr data, u64 size) const = 0;
    };

    class SK_API ResourceBuffer
    {
    public:
        bool       HasInstance() const;
        u64        GetSize() const;
        void       GetData(VoidPtr data, u64 size = 0) const;
        String     GetIdAsString() const;
        u64        GetId() const;
        void       MapFile(StringView path, u64 size = 0, u64 offset = 0);
        StringView GetMappedPath() const;
        void       SaveTo(StringView path);

        explicit operator bool() const noexcept;

        friend struct Buffers;

    private:
        u64                             id = 0;
        std::shared_ptr<BufferInstance> instance;
    };

    struct SK_API Buffers
    {
        static ResourceBuffer CreateBuffer();
        static ResourceBuffer CreateBuffer(u64 id);
        static ResourceBuffer CreateBuffer(VoidPtr data, usize size);
        static ResourceBuffer CreateBuffer(StringView origin);
    };
}
