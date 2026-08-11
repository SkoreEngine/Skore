/**
 * @file clay_impl.c
 * @brief Single translation unit that instantiates the Clay implementation.
 *
 * Clay is a single-header library: CLAY_IMPLEMENTATION must be defined in
 * exactly one TU before including clay.h. Consumers include clay.h and link
 * this static target (same pattern as stb_rect_pack).
 */

#define CLAY_IMPLEMENTATION
#include "clay.h"
