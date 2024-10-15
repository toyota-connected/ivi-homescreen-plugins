/*
 * Copyright 2020-2024 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <iostream>
#include <random>
#include <sstream>

namespace plugin_filament_view {
inline std::string generateUUID() {
  std::random_device rd;
  std::uniform_int_distribution<int> dist(0, 15);
  std::uniform_int_distribution<int> dist2(8, 11);

  std::stringstream ss;
  ss << std::hex;
  for (int i = 0; i < 8; ++i)
    ss << dist(rd);
  ss << "-";
  for (int i = 0; i < 4; ++i)
    ss << dist(rd);
  ss << "-4";  // UUID version 4
  for (int i = 0; i < 3; ++i)
    ss << dist(rd);
  ss << "-";
  ss << dist2(rd);  // UUID variant
  for (int i = 0; i < 3; ++i)
    ss << dist(rd);
  ss << "-";
  for (int i = 0; i < 12; ++i)
    ss << dist(rd);
  return ss.str();
}

}  // namespace plugin_filament_view
