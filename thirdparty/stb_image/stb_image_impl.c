/**
 * @file stb_image_impl.c
 * @brief Single TU that compiles stb_image (Sean Barrett / nothings).
 *
 * Consumers include stb_image.h and link the stb_image static lib.
 * STB_IMAGE_IMPLEMENTATION is defined exactly once here.
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
