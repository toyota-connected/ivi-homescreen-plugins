#
# Copyright 2023-2025 Toyota Connected North America
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

# Ensure CMP0079 is NEW so linking to targets created in other directories is allowed
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

set(SDBUSCPP_BUILD_CODEGEN OFF)
set(SDBUSCPP_INSTALL OFF)
add_subdirectory(third_party/sdbus-cpp)
if (ENABLE_LTO AND IPO_SUPPORT_RESULT)
    set_property(TARGET sdbus-c++ PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()
target_link_libraries(sdbus-c++-objlib PUBLIC toolchain::toolchain)
target_link_libraries(sdbus-c++ PUBLIC toolchain::toolchain)
install(TARGETS toolchain EXPORT sdbus-c++-targets)

target_sources(plugin_common PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sdbus.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/utils.cc
)
target_include_directories(plugin_common PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

target_link_libraries(plugin_common PUBLIC
        sdbus-c++
)