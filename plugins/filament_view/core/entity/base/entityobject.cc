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
#include <core/utils/uuidGenerator.h>
#include <plugins/common/common.h>
#include <utility>

namespace plugin_filament_view {

/////////////////////////////////////////////////////////////////////////////////////////
EntityObject::EntityObject(std::string name)
    : global_guid_(generateUUID()), name_(std::move(name)) {}

/////////////////////////////////////////////////////////////////////////////////////////
EntityObject::EntityObject(std::string name, std::string global_guid)
    : global_guid_(std::move(global_guid)), name_(std::move(name)) {}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::vOverrideName(const std::string& name) {
  name_ = name;
}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::vOverrideGlobalGuid(const std::string& global_guid) {
  global_guid_ = global_guid;
}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::DeserializeNameAndGlobalGuid(
    const flutter::EncodableMap& params) {
  if (auto itName = params.find(flutter::EncodableValue(kName));
      itName != params.end() && !itName->second.IsNull()) {
    // they're requesting entity be named what they want.
    std::string requestedName = std::get<std::string>(itName->second);

    if (!requestedName.empty()) {
      vOverrideName(requestedName);
      SPDLOG_INFO("OVERRIDING NAME: {}", requestedName);
    }
  }

  if (auto itGUID = params.find(flutter::EncodableValue(kGlobalGuid));
      itGUID != params.end() && !itGUID->second.IsNull()) {
    // they're requesting entity have a guid they desire.
    // Note! There's no clash checking here.
    std::string requestedGlobalGUID = std::get<std::string>(itGUID->second);
    if (!requestedGlobalGUID.empty()) {
      vOverrideGlobalGuid(requestedGlobalGUID);
      SPDLOG_INFO("OVERRIDING GLOBAL GUID: {}", requestedGlobalGUID);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::vDebugPrintComponents() const {
  spdlog::debug("EntityObject Name \'{}\' UUID {} ComponentCount {}", name_,
                global_guid_, components_.size());

  for (const auto& component : components_) {
    spdlog::debug("\tComponent Type \'{}\' Name \'{}\'",
                  component->GetRTTITypeName(), component->GetName());
    component->DebugPrint("\t\t");
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void EntityObject::vShallowCopyComponentToOther(size_t staticTypeID,
                                                EntityObject& other) const {
  const auto component = GetComponentByStaticTypeID(staticTypeID);
  if (component == nullptr) {
    spdlog::warn("Unable to clone component of {}", staticTypeID);
    return;
  }

  other.vAddComponent(std::shared_ptr<Component>(component->Clone()));
}

}  // namespace plugin_filament_view