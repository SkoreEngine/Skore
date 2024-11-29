#pragma once

#include "Skore/Common.hpp"

namespace Skore
{

    class SK_API TransactionScope
    {
    public:

        void Undo();
        void Redo();

    private:


    };

}
