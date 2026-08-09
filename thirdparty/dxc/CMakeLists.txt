# DirectX Shader Compiler (DXC) — vendored from skore main (ThirdParty/dxc).
#
# Header-only INTERFACE target: consumers include <dxc/dxcapi.h> (and
# <dxc/WinAdapter.h>) after linking against `dxc`.
#
# The platform runtime shared library is NOT linked; it is copied next to the
# built plugins by sk_copy_dxc_shared_library(<target>) so a plugin can load
# dxcompiler at runtime (see cmake/cmake_functions.cmake).
#
# Vendored from main: include/dxc/{dxcapi.h,WinAdapter.h} and
# bin/win-x64/dxcompiler.dll. Linux runtime bin/linux-x64/{libdxcompiler.so,
# libdxil.so} is vendored from the official Microsoft DXC v1.9.2607 linux_x64
# release (stripped). LICENSE.TXT (LLVM/NCSA) and ThirdPartyNotices.txt are the
# license/attribution texts from that release tag.

add_library(dxc INTERFACE)
target_include_directories(dxc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
