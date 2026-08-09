file(GLOB_RECURSE SKORE_VULKAN_RENDER_DEVICE_SOURCES *.h *.c)

sk_add_plugin(vulkan-render-device SOURCES ${SKORE_VULKAN_RENDER_DEVICE_SOURCES})

# Vulkan backend deps: volk meta-loader (static), VMA (header-only allocator),
# Vulkan-Headers. Mirrors skore main (SkoreRuntime links volk vma vulkan-sdk).
target_link_libraries(sk-vulkan-render-device PUBLIC
    volk
    vma
    vulkan-sdk
)
