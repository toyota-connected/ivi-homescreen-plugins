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

#include "filament_system.h"
#include <filament/Engine.h>
#include <filament/Scene.h>

#include <core/entity/derived/shapes/baseshape.h>
#include <core/systems/base/system.h>
#include <core/systems/derived/material_system.h>
#include <list>

namespace plugin_filament_view {

class BaseShape;

class ShapeSystem : public System {
  public:
    ShapeSystem() = default;

    void addShapesToScene(std::vector<std::shared_ptr<BaseShape>>* shapes);

    void addShapeToScene(const std::shared_ptr<BaseShape>& shape);

    // Disallow copy and assign.
    ShapeSystem(const ShapeSystem&) = delete;
    ShapeSystem& operator=(const ShapeSystem&) = delete;

    // will add/remove already made entities to/from the scene
    void ToggleAllShapesInScene(bool enable) const;
    void ToggleSingleShapeInScene(const EntityGUID guid, bool enable) const;

    void RemoveAllShapesInScene();

    // Creates the derived class of BaseShape based on the map data sent in, does
    // not add it to any list only returns the shape for you, Also does not build
    // the data out, only stores it for building when ready.
    static std::unique_ptr<BaseShape> poDeserializeShapeFromData(
      const flutter::EncodableMap& mapData
    );

    void onSystemInit() override;
    void update(double deltaTime) override;
    void onDestroy() override;
    void debugPrint() override;

  private:
    // filamentEngine, RenderableManager, EntityManager, TransformManager
    smarter_shared_ptr<FilamentSystem> _filament;
    smarter_raw_ptr<filament::Engine> _engine;
    smarter_raw_ptr<filament::RenderableManager> _rcm;
    smarter_raw_ptr<utils::EntityManager> _em;
    smarter_raw_ptr<filament::TransformManager> _tm;

    bool hasShape(const EntityGUID guid) const;
    BaseShape* getShape(const EntityGUID guid) const;

    std::vector<EntityGUID> _shapes;
};
}  // namespace plugin_filament_view
