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
  // Overloading the == operator to compare based on global_guid_
  bool operator==(const EntityObject& other) const {
    return global_guid_ == other.global_guid_;
  }

  // Pass in the <DerivedClass>::StaticGetTypeID()
  // Returns true if valid, false if not found.
  bool HasComponentByStaticTypeID(size_t staticTypeID) const {
    for (const auto& item : components_) {
      if (item->GetTypeID() == staticTypeID) {
        return true;
      }
    }
    return false;
  }

 protected:
  EntityObject(std::string name);
  virtual ~EntityObject() {
    for (auto it : components_) {
      delete it;
    }
    components_.clear();
  }

  EntityObject(const EntityObject&) = delete;
  EntityObject& operator=(const EntityObject&) = delete;

  virtual void DebugPrint() const = 0;

  const std::string& GetGlobalGuid() const { return global_guid_; }

  const std::string& GetName() const { return name_; }

  void vAddComponent(Component* component) {
    component->entityOwner_ = this;
    components_.emplace_back(component);
  }

  // Pass in the <DerivedClass>::StaticGetTypeID()
  // Returns component if valid, nullptr if not found.
  Component* GetComponentByStaticTypeID(size_t staticTypeID) {
    for (const auto& item : components_) {
      if (item->GetTypeID() == staticTypeID) {
        return item;
      }
    }
    return nullptr;
  }

  Component* GetComponentByStaticTypeID(size_t staticTypeID) const {
    for (const auto& item : components_) {
      if (item->GetTypeID() == staticTypeID) {
        return item;
      }
    }
    return nullptr;
  }

  void vDebugPrintComponents() const;

  // finds the size_t staticTypeID in the component list
  // and creates a copy and assigns to the others list
  void vShallowCopyComponentToOther(size_t staticTypeID,
                                    EntityObject& other) const;

 private:
  std::string global_guid_;
  std::string name_;

  // Vector for now, we shouldn't be adding and removing
  // components frequently during runtime.
  //
  // In the future this is probably a map<type, vector_or_list<Comp*>>
  std::vector<Component*> components_;
};
}  // namespace plugin_filament_view