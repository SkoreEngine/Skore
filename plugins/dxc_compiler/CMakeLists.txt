file(GLOB_RECURSE SKORE_DXC_COMPILER_SOURCES *.h *.c)

sk_add_plugin(dxc-compiler SOURCES ${SKORE_DXC_COMPILER_SOURCES})

# DXC (DirectX Shader Compiler) vendored headers: <dxc/dxcapi.h> /
# <dxc/WinAdapter.h>. dxcapi.h is C++-only, so the C plugin drives DXC through
# its own COM vtables; the `dxc` INTERFACE target is linked so the header
# include dir is available to this plugin and to sk-dxc-compiler-lib consumers.
target_link_libraries(sk-dxc-compiler PUBLIC dxc)

# Copy the platform DXC shared library (win-x64/dxcompiler.dll today; Unix/macOS
# emit a configure WARNING until those runtimes are vendored) into the plugins
# output dir beside the built plugin, so sk_dxc_compiler_api_t::init can load it.
sk_copy_dxc_shared_library(sk-dxc-compiler)
