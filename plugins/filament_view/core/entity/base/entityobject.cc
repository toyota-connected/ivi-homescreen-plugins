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
#include "entityobject.h"

#include <core/include/literals.h>
#include <core/systems/ecs.h>
#include <core/utils/deserialize.h>
#include <core/utils/uuidGenerator.h>
#include <plugins/common/common.h>
#include <utility>

namespace plugin_filament_view {

EntityObject::EntityObject()
  : guid_(generateUUID()),
    name(std::string()) {}

EntityObject::EntityObject(std::string name)
  : guid_(generateUUID()),
    name(std::move(name)) {}

EntityObject::EntityObject(EntityGUID guid)
  : guid_(guid),
    name(std::string()) {}

EntityObject::EntityObject(std::string name, EntityGUID guid)
  : guid_(guid),
    name(std::move(name)) {}

EntityObject::EntityObject(const EntityDescriptor& descriptor)
  : guid_(descriptor.guid),
    name(descriptor.name) {}

EntityObject::EntityObject(const flutter::EncodableMap& params)
  : guid_(kNullGuid),
    name(std::string()) {
  deserializeFrom(params);
  assert(guid_ != kNullGuid);
}

EntityDescriptor EntityObject::DeserializeNameAndGuid(const flutter::EncodableMap& params) {
  EntityDescriptor descriptor;

  // Deserialize name
  if (const auto itName = params.find(flutter::EncodableValue(kName));
      itName != params.end() && !itName->second.IsNull()) {
    // they're requesting entity be named what they want.

    if (auto requestedName = std::get<std::string>(itName->second); !requestedName.empty()) {
      descriptor.name = requestedName;
    }
  }

  // Deserialize guid
  Deserialize::DecodeParameterWithDefaultInt64(kGuid, &descriptor.guid, params, kNullGuid);

  if (descriptor.guid == kNullGuid) {
    spdlog::warn("Failed to deserialize guid, generating new one");
    descriptor.guid = generateUUID();
  }

  return descriptor;
}

void EntityObject::deserializeFrom(const flutter::EncodableMap& params) {
  // Deserialize name and guid
  auto descriptor = DeserializeNameAndGuid(params);
  name = descriptor.name;
  guid_ = descriptor.guid;
}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::debugPrintComponents() const {
  if (!isInitialized()) {
    spdlog::debug("EntityObject '{}'({}) is not initialized", name, guid_);
    return;
  }

  std::vector<std::string> componentNames;
  // get list of component pointers
  const auto components = ecs->getComponentsOfEntity(guid_);
  for (const auto& component : components) {
    if (component == nullptr) {
      spdlog::warn("Component is null");
      continue;
    }
    componentNames.push_back(component->getTypeName());
  }

  spdlog::debug(
    "EntityObject '{}'({}) has {} components: {}", name, guid_, componentNames.size(),
    fmt::join(componentNames, ", ")
  );
}

void EntityObject::debugPrint() const {
  spdlog::debug("EntityObject '{}'({}), {}initialized", name, guid_, isInitialized() ? "" : "not ");
  debugPrintComponents();
}

std::shared_ptr<Component> EntityObject::getComponent(size_t staticTypeID) const {
  if (isInitialized()) {
    return ecs->getComponent(guid_, staticTypeID);
  } else {
    if (auto it = _tmpComponents.find(staticTypeID); it != _tmpComponents.end()) {
      return it->second;
    } else {
      return nullptr;
    }
  }
}

[[nodiscard]] bool EntityObject::hasComponent(size_t staticTypeID) const {
  return ecs->hasComponent(guid_, staticTypeID);
}

void EntityObject::ShallowCopyComponentToOther(size_t staticTypeID, EntityObject& other) const {
  // assertInitialized();
  const auto component = getComponent(staticTypeID);
  if (component == nullptr) {
    spdlog::warn("Unable to clone component of {}", staticTypeID);
    return;
  }

  ecs->addComponent(other.guid_, std::shared_ptr<Component>(component->Clone()));
}

void EntityObject::addComponent(size_t staticTypeID, const std::shared_ptr<Component>& component) {
  if (isInitialized()) {
    ecs->addComponent(guid_, component);
  } else {
    // batch the component to be added after initialization
    _tmpComponents[staticTypeID] = component;
  }
}

void EntityObject::onAddComponent(const std::shared_ptr<Component>& component) {
  assertInitialized();
  component->entityOwner_ = this;
}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::onInitialize() {
  // add all components that were added before initialization
  for (const auto& [staticTypeID, component] : _tmpComponents) {
    ecs->addComponent(guid_, component);
  }
  _tmpComponents.clear();
}

}  // namespace plugin_filament_view
