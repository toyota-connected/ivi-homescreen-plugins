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
find_package(PkgConfig REQUIRED)

pkg_check_modules(PIPEWIRE IMPORTED_TARGET REQUIRED libpipewire-0.3)
pkg_check_modules(JPEG IMPORTED_TARGET REQUIRED libjpeg)

add_library(plugin_camera_pipewire STATIC
        camera_plugin_c_api.cc
        camera_plugin.cc
        messages.g.cc
        CameraStream.cc
        CameraStream.h
        CameraManager.cc
        CameraManager.h
)

target_include_directories(plugin_camera_pipewire PUBLIC
        include
)

target_link_libraries(plugin_camera_pipewire PUBLIC
        PkgConfig::PIPEWIRE
        PkgConfig::JPEG
        flutter
        platform_homescreen
        plugin_common
)
