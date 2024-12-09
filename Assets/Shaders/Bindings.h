#ifndef SK_BINDINGS_H
#define SK_BINDINGS_H

#define SK_BINDLESS_SPACE 2
#define SK_BINDLESS_TEXTURES_SLOT 0


#ifndef __cplusplus

Texture2D bindlessTextures[] : register(t0, space2);

#endif

#endif
