include_guard()

# The plugin renders into an FBO using the engine's GL context — no
# per-plugin EGL context, no Wayland subsurface. The compositor composites
# the resulting texture onto the scene at the layer rect.

add_library(plugin_layer_playground_view STATIC
        layer_playground_view_plugin.cc
        layer_playground_view_plugin_c_api.cc
        # Optional Vulkan render path: reuses the wayland-vulkan backend's device
        # (via Backend::GetVulkanContext) to write the gradient into a VkImage
        # when there is no engine GL context. Uses vulkan.hpp + the process-
        # shared dynamic dispatcher owned by the shell — no libvulkan link.
        layer_playground_vulkan.cc
)

target_include_directories(plugin_layer_playground_view PUBLIC
        .
        include
        # ivi-homescreen's vendored Vulkan-Headers, matching the version the
        # backend initializes the shared dispatcher against.
        ${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include
)

target_link_libraries(plugin_layer_playground_view PUBLIC
        flutter
        platform_homescreen
        GLESv2
)

if (BUILD_COMPOSITOR)
        # Direct-scanout stub (IVI_PV_DMABUF): allocates a scanout gbm_bo and
        # exports it as a dma-buf for the DRM compositor's KMS-plane path.
        target_link_libraries(plugin_layer_playground_view PUBLIC gbm)
endif ()
