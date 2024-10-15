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

# Target executable names.
set(CEF_TARGET "${WEBVIEW_SUBPROCESS_PROJECT_NAME}")

# Determine the target output directory.
set(CEF_TARGET_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}")

# Executable target.
add_executable(${CEF_TARGET} flutter_webview_subprocess.cc)

target_include_directories(${CEF_TARGET} PRIVATE ${CEF_ROOT})

add_dependencies(${CEF_TARGET} libcef_dll_wrapper)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
  target_compile_options(libcef_dll_wrapper PUBLIC "-Wno-deprecated-builtins")
endif()
endif()

target_link_directories(${CEF_TARGET} PUBLIC
        ${CEF_ROOT}/${CEF_BUILD_TYPE}
)
target_link_libraries(${CEF_TARGET} PRIVATE
        cef
        libcef_dll_wrapper
)

install(TARGETS ${CEF_TARGET} DESTINATION bin)
