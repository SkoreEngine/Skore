# VMA (AMD) — vendored from skore main, MIT. Vulkan Memory Allocator.
# Header-only INTERFACE; consumers implement vk_mem_alloc.h in their own TU.
# Include dir is SYSTEM: plugin TUs including vk_mem_alloc.h stay quiet under
# first-party -Werror / clang-tidy (thirdparty code is exempt).

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
