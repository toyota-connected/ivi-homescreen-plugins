#
# Copyright 2024 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

find_package(PkgConfig REQUIRED)

if(NOT TARGET plugin_common)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common
                     ${CMAKE_CURRENT_BINARY_DIR}/_common)
endif()

pkg_check_modules(FLATPAK_DEPS IMPORTED_TARGET REQUIRED flatpak libxml-2.0 zlib)

add_subdirectory(cache)

add_library(plugin_flatpak STATIC
        flatpak_plugin_c_api.cc
        flatpak_plugin.cc
        flatpak_shim.cc
        messages.g.cc
        appstream_catalog.cc
        component.cc
        icon.cc
        release.cc
        screenshot.cc
        portals/portal_manager.cc
        portals/portal_proxy.cc
        portals/permissions_portal/permissions_portal.cc
        portals/permissions_portal/permissions_portal.h
        operation_tracker.cc
        operation_tracker.h
)
target_include_directories(plugin_flatpak PRIVATE include)
target_compile_options(plugin_flatpak PRIVATE
        -Wno-deprecated-declarations
)
target_link_libraries(plugin_flatpak PUBLIC
        PkgConfig::FLATPAK_DEPS
        plugin_common_curl
        plugin_common_glib
        plugin_flatpak_cache
        platform_homescreen
)

if (BUILD_UNIT_TESTS)
    add_subdirectory(test)
endif ()