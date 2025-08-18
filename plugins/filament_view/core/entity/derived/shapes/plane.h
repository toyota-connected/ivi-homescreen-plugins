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

#include "baseshape.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

namespace plugin_filament_view {

class Plane : public BaseShape {
  public:
    /// @brief Constructor for Plane. Generates a GUID and has an empty name.
    Plane()
      : BaseShape(ShapeType::Plane) {}
    /// @brief Constructor for Plane with a name. Generates a unique GUID.
    explicit Plane(std::string name)
      : BaseShape(name, ShapeType::Plane) {}
    /// @brief Constructor for Plane with GUID. Name is empty.
    explicit Plane(EntityGUID guid)
      : BaseShape(guid, ShapeType::Plane) {}
    /// @brief Constructor for Plane with a name and GUID.
    explicit Plane(std::string name, EntityGUID guid)
      : BaseShape(name, guid, ShapeType::Plane) {}
    ~Plane() override = default;

    // Disallow copy and assign.
    Plane(const Plane&) = delete;
    Plane& operator=(const Plane&) = delete;

    bool bInitAndCreateShape(::filament::Engine* engine_, FilamentEntity entityObject) override;

  private:
    void createDoubleSidedPlane(::filament::Engine* engine_);

    void createSingleSidedPlane(::filament::Engine* engine_);
};

}  // namespace plugin_filament_view
