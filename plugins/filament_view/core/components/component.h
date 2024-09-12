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
#include <string>
#include <typeinfo>

namespace plugin_filament_view {
class EntityObject;
}

namespace plugin_filament_view {

class Component {
  friend class EntityObject;

 public:
  [[nodiscard]] std::string GetRTTITypeName() {
    if (rttiTypeName_.empty()) {
      rttiTypeName_ = std::string(typeid(*this).name());
    }
    return rttiTypeName_;
  }

  [[nodiscard]] std::string GetName() { return name_; }

 protected:
  Component(const std::string& name) : name_(name) {}
  virtual ~Component() = default;

  virtual const std::type_info& GetType() const { return typeid(*this); }

  virtual void DebugPrint(const std::string tabPrefix) const = 0;

  const EntityObject* GetOwner() const { return entityOwner_; }

 private:
  std::string rttiTypeName_;
  std::string name_;

  EntityObject* entityOwner_;
};

}  // namespace plugin_filament_view