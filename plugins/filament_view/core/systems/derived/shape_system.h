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

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <core/systems/base/ecsystem.h>
#include <list>

#include "core/entity/shapes/baseshape.h"
#include "core/systems/derived/material_system.h"

namespace plugin_filament_view {

namespace shapes {
class BaseShape;
}

class ShapeSystem : public ECSystem {
 public:
  ShapeSystem() = default;

  void addShapesToScene(
      std::vector<std::unique_ptr<shapes::BaseShape>>* shapes);

  // Disallow copy and assign.
  ShapeSystem(const ShapeSystem&) = delete;
  ShapeSystem& operator=(const ShapeSystem&) = delete;

  // will add/remove already made entities to/from the scene
  void vToggleAllShapesInScene(bool bValue);

  void vRemoveAllShapesInScene();

  // Creates the derived class of BaseShape based on the map data sent in, does
  // not add it to any list only returns the shape for you, Also does not build
  // the data out, only stores it for building when ready.
  static std::unique_ptr<shapes::BaseShape> poDeserializeShapeFromData(
      const std::string& flutter_assets_path,
      const flutter::EncodableMap& mapData);

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(ShapeSystem).hash_code();
  }

  void vInitSystem() override;
  void vUpdate(float fElapsedTime) override;
  void vShutdownSystem() override;
  void DebugPrint() override;

 private:
  std::list<std::unique_ptr<shapes::BaseShape>> shapes_;
};
}  // namespace plugin_filament_view