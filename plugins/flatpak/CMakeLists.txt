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
pkg_check_modules(FLATPAK IMPORTED_TARGET REQUIRED flatpak)
pkg_check_modules(XML2 IMPORTED_TARGET REQUIRED libxml-2.0)

add_library(plugin_flatpak STATIC
        flatpak_plugin_c_api.cc
        flatpak_plugin.cc
        messages.cc
)

target_include_directories(plugin_flatpak PRIVATE include)

target_link_libraries(plugin_flatpak PUBLIC
        flutter
        platform_homescreen
        PkgConfig::FLATPAK
        PkgConfig::XML2
)
