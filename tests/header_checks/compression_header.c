/**
 * @file compression_header.c
 * @brief Trivial TU that compiles the public compression interface header
 *        standalone (APX-169 frozen-interface verification).
 *
 * foundation/compression.h may rely only on common.h and allocator.h and must
 * compile clean under the project's warnings-as-errors flags. This TU makes
 * any self-containment or warning regression a build failure; it defines
 * nothing and is never linked or run.
 */

#include "compression.h"
