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

# --- MapLibre-native (Inc3, opt-in) ------------------------------------------
# When the fork's source root + build dir are supplied, build an ISOLATED
# sub-library (maplibre_mbgl) that owns all mbgl + MapLibre-vulkan.hpp code and
# links libmbgl-core. Isolation is required because MapLibre vendors an OLDER
# vulkan.hpp (symbol vk::defaultDispatchLoaderDynamic) than ivi-homescreen
# (vk::detail::defaultDispatchLoaderDynamic) — mixing the two in one TU breaks.
# The sub-library exposes only the Vulkan-free mbgl/maplibre_map_renderer.h to
# the plugin, and links mbgl-core PRIVATE so MapLibre's Vulkan headers never
# reach the plugin's own (ivi-Vulkan) translation units.
if(MAPLIBRE_NATIVE_DIR AND MAPLIBRE_NATIVE_BUILD_DIR)
    find_package(CURL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(JPEG REQUIRED)
    find_package(ICU REQUIRED COMPONENTS i18n uc)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MLN_UV REQUIRED IMPORTED_TARGET libuv)
    pkg_check_modules(MLN_WEBP REQUIRED IMPORTED_TARGET libwebp)

    # MapboxCoreTargets.cmake references bare target names `uv` and `webp`;
    # front them with the proper PkgConfig imported targets (full link paths).
    if(NOT TARGET uv)
        add_library(uv INTERFACE IMPORTED)
        target_link_libraries(uv INTERFACE PkgConfig::MLN_UV)
    endif()
    if(NOT TARGET webp)
        add_library(webp INTERFACE IMPORTED)
        target_link_libraries(webp INTERFACE PkgConfig::MLN_WEBP)
    endif()

    include(${MAPLIBRE_NATIVE_BUILD_DIR}/MapboxCoreTargets.cmake)

    add_library(maplibre_mbgl STATIC
            mbgl/external_headless_backend.cc
            mbgl/maplibre_map_renderer.cc)
    target_include_directories(maplibre_mbgl
            PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/mbgl   # Vulkan-free boundary header
            PRIVATE ${MAPLIBRE_NATIVE_DIR}/include
                    ${MAPLIBRE_NATIVE_DIR}/src)
    target_link_libraries(maplibre_mbgl PRIVATE mbgl-core)
    # mbgl headers require C++20 (concepts, std::ranges/numbers/span, defaulted
    # operator==); ivi's default is C++17. Scope it to this sub-library only.
    set_target_properties(maplibre_mbgl PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON)
    # mbgl-core is built with -fno-rtti (no typeinfo emitted); subclasses of its
    # polymorphic types must match or the base typeinfo is an undefined ref.
    target_compile_options(maplibre_mbgl PRIVATE -fno-rtti)

    target_link_libraries(plugin_maplibre_view PUBLIC maplibre_mbgl)
    target_compile_definitions(plugin_maplibre_view PRIVATE MAPLIBRE_VIEW_HAVE_MBGL=1)
    message(STATUS "maplibre_view: linking MapLibre-native (isolated maplibre_mbgl) from ${MAPLIBRE_NATIVE_BUILD_DIR}")
endif()
