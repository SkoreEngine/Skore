# DirectX Shader Compiler (DXC) — vendored from skore main (ThirdParty/dxc).
#
# Header-only INTERFACE target: consumers include <dxc/dxcapi.h> (and
# <dxc/WinAdapter.h>) after linking against `dxc`.
#
# The platform runtime shared library is NOT linked; it is copied next to the
# built plugins by sk_copy_dxc_shared_library(<target>) so a plugin can load
# dxcompiler at runtime (see cmake/cmake_functions.cmake).
#
# Vendored runtimes (copied at build time by sk_copy_dxc_shared_library):
#   bin/win-x64/dxcompiler.dll          — Microsoft DXC win-x64
#   bin/macOS/libdxcompiler.dylib       — universal (x86_64+arm64) from LunarG
#                                         Vulkan SDK macOS (e.g. 1.4.357.0)
#   bin/linux-x64/libdxcompiler.so      — Microsoft DXC linux_x64
#   bin/linux-x64/libdxil.so            —   (linux_dxc_2026_05_26.x86_64 release)
# Headers: include/dxc/{dxcapi.h,WinAdapter.h}. LICENSE.TXT (LLVM/NCSA) and
# ThirdPartyNotices.txt are the license/attribution texts from the DXC release.

add_library(dxc INTERFACE)
target_include_directories(dxc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
