# Clay (vendored)

Immediate-mode UI layout library (single-header C). Immediate-mode sibling of
the engine's retained-mode UI tree; used by the sk-ui plugin as the layout
engine (APX-213).

- Upstream: https://github.com/nicbarker/clay
- Version: **v0.14** (release tag `v0.14`, commit `b25a31c1a152915cd7dd6796e6592273e5a10aac`)
- License: zlib/libpng (see `LICENSE.md`)
- Files kept: `clay.h` (single header, unmodified from the tag), this README,
  the license, and `clay_impl.c` / `CMakeLists.txt` (skore build glue).

Usage follows the upstream contract: define `CLAY_IMPLEMENTATION` in exactly
one translation unit (`clay_impl.c` here), include `clay.h` everywhere else,
link the `clay` static target. Requires C99 (or C++20 / MSVC).

## Version record

| Field     | Value                                                            |
|-----------|------------------------------------------------------------------|
| tag       | v0.14                                                            |
| commit    | b25a31c1a152915cd7dd6796e6592273e5a10aac                         |
| released  | 2025-06-06                                                       |
| upstream  | https://github.com/nicbarker/clay                                 |
| raw file  | https://raw.githubusercontent.com/nicbarker/clay/v0.14/clay.h     |
| license   | zlib/libpng (LICENSE.md)                                          |
