#
# Copyright 2026 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#

include_guard()

# MapLibre Native platform view (Vulkan / Mode A).
#
# The view reuses the compositor backend's own Vulkan device (via
# Backend::GetVulkanContext) and renders into a VkImage the compositor samples
# directly through ICompositorSurface::GetVulkanImage — no second device, no
# dma-buf, no GL interop. It is installed through the PlatformViewRegistry
# factory seam (see maplibre_view_plugin_c_api.cc), not the legacy dispatch.
#
# Today the renderer paints a placeholder; MapLibre-native (mbgl::Map on the
# injected device) and its OpenGL sibling path are wired in later. Requires
# BUILD_COMPOSITOR (enforced by a #error in maplibre_view_plugin.h).

add_library(plugin_maplibre_view STATIC
        maplibre_view_plugin.cc
        maplibre_view_plugin_c_api.cc
        maplibre_vulkan_renderer.cc
)

target_include_directories(plugin_maplibre_view PUBLIC
        .
        include
        # ivi-homescreen's vendored Vulkan-Headers, matching the version the
        # backend initializes the shared dispatcher against.
        ${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include
)

target_link_libraries(plugin_maplibre_view PUBLIC
        flutter
        platform_homescreen
)
