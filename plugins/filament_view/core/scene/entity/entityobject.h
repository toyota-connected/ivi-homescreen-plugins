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

#include <string>
#include <vector>

#include "core/components/component.h"

namespace plugin_filament_view {

class EntityObject {
 public:
 protected:
  EntityObject(std::string name);
  virtual ~EntityObject() {
    for (auto it : components_) {
      delete it;
    }
    components_.clear();
  }

  virtual void DebugPrint() const = 0;

  const std::string& GetGlobalGuid() const { return global_guid_; }

  const std::string& GetName() const { return name_; }

  void vAddComponent(Component* component) {
    component->entityOwner_ = this;
    components_.emplace_back(component);
  }

  void vDebugPrintComponents() const;

 private:
  std::string global_guid_;
  std::string name_;

  // Vector for now, we shouldnt be adding and removing
  // components frequently during runtime.
  //
  // In the future this is probably a map<type, vector<Comp*>>
  std::vector<Component*> components_;
};
}  // namespace plugin_filament_view