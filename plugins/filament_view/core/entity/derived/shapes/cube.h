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

class Cube : public BaseShape {
  public:
    /// @brief Constructor for Cube. Generates a GUID and has an empty name.
    Cube()
      : BaseShape(ShapeType::Cube) {}
    /// @brief Constructor for Cube with a name. Generates a unique GUID.
    explicit Cube(std::string name)
      : BaseShape(name, ShapeType::Cube) {}
    /// @brief Constructor for Cube with GUID. Name is empty.
    explicit Cube(EntityGUID guid)
      : BaseShape(guid, ShapeType::Cube) {}
    /// @brief Constructor for Cube with a name and GUID.
    explicit Cube(std::string name, EntityGUID guid)
      : BaseShape(name, guid, ShapeType::Cube) {}
    ~Cube() override = default;

    // Disallow copy and assign.
    Cube(const Cube&) = delete;
    Cube& operator=(const Cube&) = delete;

    void debugPrint(const char* tag) const override;

    bool bInitAndCreateShape(::filament::Engine* engine_, FilamentEntity entityObject) override;

  private:
    void createDoubleSidedCube(::filament::Engine* engine_);

    void createSingleSidedCube(::filament::Engine* engine_);
};

}  // namespace plugin_filament_view
