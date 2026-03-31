#
# Copyright 2026 Toyota Connected North America
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

include_guard()

option(BUILD_SHARED_PLUGIN "Build as a standalone shared (.so) library" OFF)
if(BUILD_SHARED_PLUGIN)
    set(_lib_type SHARED)
    # MODE: Shared plugin
    #   The plugin is built as a shared library (.so) that can be loaded at runtime by the host process.
    #   In this mode, the plugin must not link directly against any host libraries (like flutter or platform_homescreen)
    #   since those symbols will be resolved at load time from the host process.
    #   Instead, the plugin should declare these dependencies as INTERFACE libraries
    #   with the appropriate include directories so that the plugin can be compiled against the correct headers,
    #   but linked without actual library dependencies.
    #   
    #   Required variables
    #   - IVI_HOMESCREEN_INCLUDE_DIR: Path to the ivi-homescreen headers, which must include the platform_homescreen headers.
    #
    
    # Check variables
    if(NOT DEFINED IVI_HOMESCREEN_INCLUDE_DIR)
        message(FATAL_ERROR
            "IVI_HOMESCREEN_INCLUDE_DIR must point to the ivi-homescreen "
            "platform headers (flutter_homescreen.h, etc.) when building "
            "the shared plugin outside of the ivi-homescreen CMake tree.")
    endif()
else()
    set(_lib_type STATIC)
    # MODE: Static plugin
    #   The plugin is included as part of the `ivi-homescreen` compilation.
    #   The plugin will be linked directly into the homescreen binary,
    #   so it can link against any of the homescreen's libraries
    #   (like flutter and platform_homescreen)/
    #
    #   Required variables:
    #   - (none)
    #

    # Check target
    if(NOT TARGET platform_homescreen)
        message(FATAL_ERROR
            "The platform_homescreen target must be available when building "
            "the static plugin. Make sure this plugin is being built as part "
            "of the ivi-homescreen CMake build and that the platform_homescreen "
            "target is included before this plugin.")
    endif()
endif()


#
# STEP 0: Load the plugin configuration
#
include(plugin.cmake)

# Validate the plugin.cmake file. Make sure it has the following defines:
set(_plugin_cmake_defines
        PLUGIN_DATA_SOURCES
        PLUGIN_DATA_LIBRARIES
        PLUGIN_NAME
        PLUGIN_TARGET_NAME
        PLUGIN_REGISTER_ENDPOINT
        PLUGIN_HEADER
)

set(_plugin_cmake_macros
        PLUGIN_STEP_DEPENDENCIES
        PLUGIN_STEP_TARGETS
)

# If any of these are missing, the plugin.cmake file is not valid and the build
# should fail.
foreach(define IN LISTS _plugin_cmake_defines)
    if(NOT DEFINED ${define})
        message(FATAL_ERROR "The plugin.cmake file is missing the required define: ${define}")
    endif()
endforeach()

foreach(macro IN LISTS _plugin_cmake_macros)
    if(NOT COMMAND ${macro})
        message(FATAL_ERROR "The plugin.cmake file is missing the required macro: ${macro}")
    endif()
endforeach()

#
# STEP 1: Setup static/dynamic plugin resolution
#
if(BUILD_SHARED_PLUGIN)
    # Configure homescreen library
    add_library(platform_homescreen INTERFACE)
    target_include_directories(platform_homescreen INTERFACE
        ${IVI_HOMESCREEN_INCLUDE_DIR}
        ${IVI_HOMESCREEN_INCLUDE_DIR}/shell/platform/homescreen
        ${IVI_HOMESCREEN_INCLUDE_DIR}/shell/platform/homescreen/client_wrapper/include
        ${IVI_HOMESCREEN_INCLUDE_DIR}/shell/platform/homescreen/public
    )

    # Rename PLUGIN_REGISTER_ENDPOINT to standard `FlutterPluginRegister` for shared plugins
    set(PLUGIN_REGISTER_ENDPOINT "FlutterPluginRegister")
endif()


#
# STEP 2: Declare plugin dependencies
#
if(BUILD_SHARED_PLUGIN) 
    find_package(PkgConfig REQUIRED)
    # NOTE: in static mode, homescreen is expected to do this
endif()
# TODO: gather all modules required when building in bulk (static plugins)
PLUGIN_STEP_DEPENDENCIES()



#
# STEP 3: Declare plugin libraries
#
add_library(${PLUGIN_TARGET_NAME} ${_lib_type}
        ${PLUGIN_DATA_SOURCES}
)

target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(BUILD_SHARED_PLUGIN)
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE BUILD_SHARED_PLUGIN=1)
endif()


target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

PLUGIN_STEP_TARGETS()



#
# STEP 4: Link dependencies
#
set(_plugin_libraries
        flutter
        platform_homescreen
        ${PLUGIN_DATA_LIBRARIES}
)

# Link libraries
if(BUILD_SHARED_PLUGIN)
    # For a shared plugin libraries are linked as PRIVATE
    set(_link_type PRIVATE)
else()
    # For a static plugin libraries are linked as PUBLIC to ensure they are propagated to the final homescreen binary
    set(_link_type PUBLIC)
endif()

target_link_libraries(${PLUGIN_TARGET_NAME} ${_link_type} ${_plugin_libraries})



#
# STEP 5: Validate
#

# Make sure that the output target has a FlutterPluginRegister symbol exposed
if(BUILD_SHARED_PLUGIN)
    add_custom_command(TARGET ${PLUGIN_TARGET_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Validating plugin ${PLUGIN_NAME} for symbol: ${PLUGIN_REGISTER_ENDPOINT}"
            COMMAND ${CMAKE_COMMAND} -E sh -c "nm -D \$<TARGET_FILE:${PLUGIN_TARGET_NAME}> | grep ${PLUGIN_REGISTER_ENDPOINT} || (echo 'Error: Symbol ${PLUGIN_REGISTER_ENDPOINT} not found in plugin ${PLUGIN_NAME}' && exit 1)"
    )
endif()