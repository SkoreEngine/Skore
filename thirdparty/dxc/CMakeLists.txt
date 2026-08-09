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
# bin/win-x64/dxcompiler.dll. Upstream ships no license/NOTICE file in main's
# vendor tree; the dxcapi.h header carries the UIUC/NCSA license notice text.

add_library(dxc INTERFACE)
target_include_directories(dxc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
