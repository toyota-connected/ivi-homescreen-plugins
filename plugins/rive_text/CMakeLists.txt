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

include_guard()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RIVE_TEXT IMPORTED_TARGET REQUIRED rive_text)

add_library(plugin_rive_text STATIC
        rive_text_plugin_c_api.cc
        rive_text_plugin.cc
        librive_text.cc
)

target_include_directories(plugin_rive_text PRIVATE
        include
)

target_link_libraries(plugin_rive_text PUBLIC
        flutter
        platform_homescreen
        plugin_common
        PkgConfig::RIVE_TEXT
)
