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

#include "ecs_message_types.h"

#include <any>
#include <iostream>
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>

namespace plugin_filament_view {

struct EnumClassHash {
  template <typename T>
  std::size_t operator()(T t) const {
    return static_cast<std::size_t>(t);
  }
};

// Message class that can hold variable data amounts
class ECSMessage {
 public:
  // Add data to the message
  template <typename T>
  void addData(ECSMessageType type, const T& value) {
    data_[type] = value;
  }

  // Get data from the message
  template <typename T>
  T getData(ECSMessageType type) const {
    auto it = data_.find(type);
    if (it != data_.end()) {
      try {
        return std::any_cast<T>(it->second);
      } catch (const std::bad_any_cast& e) {
        throw std::runtime_error("Type mismatch for key. Expected type: " +
                                 std::string(typeid(T).name()));
      }
    }
    throw std::runtime_error("Message type not found");
  }

  // Check if the message contains a specific type
  bool hasData(ECSMessageType type) const {
    return data_.find(type) != data_.end();
  }

 private:
  std::unordered_map<ECSMessageType, std::any, EnumClassHash> data_;
};
}  // namespace plugin_filament_view
