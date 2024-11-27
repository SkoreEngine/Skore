#pragma once
#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"


namespace Skore
{
    class SK_API TextureViewWindow : public EditorWindow
    {
    public:
        SK_BASE_TYPES(EditorWindow);


        void Init(u32 id, VoidPtr userData) override;
        void Draw(u32 id, bool& open) override;

        static void Open(Texture texture);
    private:
        Texture texture = {};
    };
}
