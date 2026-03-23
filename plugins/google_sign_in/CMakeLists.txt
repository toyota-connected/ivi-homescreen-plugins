#
# Copyright 2023-2024 Toyota Connected North America
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

if(NOT TARGET plugin_common)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common
                     ${CMAKE_CURRENT_BINARY_DIR}/_common)
endif()

add_library(plugin_google_sign_in STATIC
        google_sign_in_plugin_c_api.cc
        google_sign_in_plugin.cc
        messages.g.cc
)

target_include_directories(plugin_google_sign_in PRIVATE include)

target_link_libraries(plugin_google_sign_in PUBLIC
        flutter
        platform_homescreen
        plugin_common
        plugin_common_curl
)
