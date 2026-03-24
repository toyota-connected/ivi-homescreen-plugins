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

set(TESTCASE_NAME plugin_common_curl_client)

add_executable(
        ${TESTCASE_NAME}
        test_curl_client.cc
        ../curl_client.cc
)

target_link_libraries(
        ${TESTCASE_NAME}
        PRIVATE
        plugin_common_curl
        gtest_main
)

add_test(
        NAME ${TESTCASE_NAME}
        COMMAND ${TESTCASE_NAME}
)