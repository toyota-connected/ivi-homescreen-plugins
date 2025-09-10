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
#include <typeinfo>

#include <core/utils/filament_types.h>
#include <core/utils/identifiable_type.h>
#include <core/utils/smarter_pointers.h>

namespace plugin_filament_view {
class EntityObject;
}

namespace plugin_filament_view {

class Component : public IdentifiableType {
    friend class EntityObject;

  public:
    /// @brief Whether the component is enabled or not.
    /// It is the respective systems' responsibility to check this flag
    /// and act accordingly.
    bool enabled = true;

    [[nodiscard]] inline const smarter_raw_ptr<EntityObject> getOwner() const {
      return entityOwner_;
    }

    [[nodiscard]] virtual const std::type_info& getType() const { return typeid(*this); }

    virtual void debugPrint(const std::string& tabPrefix) const = 0;

    [[nodiscard]] virtual Component* Clone() const = 0;

    virtual ~Component() = default;

  protected:
    explicit Component(std::string name)
      : name_(std::move(name)),
        entityOwner_(nullptr) {}

  private:
    /// @deprecated Instead use getTypeName()
    std::string name_;

  public:
    smarter_raw_ptr<EntityObject> entityOwner_ = nullptr;
};

}  // namespace plugin_filament_view
