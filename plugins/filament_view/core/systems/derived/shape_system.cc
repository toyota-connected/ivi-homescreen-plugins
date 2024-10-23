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

#include "shape_system.h"
#include "filament_system.h"

#include <core/entity/derived/shapes/baseshape.h>
#include <core/entity/derived/shapes/cube.h>
#include <core/entity/derived/shapes/plane.h>
#include <core/entity/derived/shapes/sphere.h>
#include <core/systems/ecsystems_manager.h>
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

using shapes::BaseShape;
using utils::Entity;

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::vToggleAllShapesInScene(const bool bValue) const {
  if (bValue) {
    for (const auto& shape : shapes_) {
      shape->vAddEntityToScene();
    }
  } else {
    for (const auto& shape : shapes_) {
      shape->vRemoveEntityFromScene();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::vRemoveAllShapesInScene() {
  vToggleAllShapesInScene(false);
  shapes_.clear();
}

////////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<BaseShape> ShapeSystem::poDeserializeShapeFromData(
    const flutter::EncodableMap& mapData) {
  ShapeType type;

  // Find the "shapeType" key in the mapData
  if (const auto it = mapData.find(flutter::EncodableValue("shapeType"));
      it != mapData.end() && std::holds_alternative<int32_t>(it->second)) {
    // Check if the value is within the valid range of the ShapeType enum
    if (int32_t typeValue = std::get<int32_t>(it->second);
        typeValue > static_cast<int32_t>(ShapeType::Unset) &&
        typeValue < static_cast<int32_t>(ShapeType::Max)) {
      type = static_cast<ShapeType>(typeValue);
    } else {
      spdlog::error("Invalid shape type value: {}", typeValue);
      return nullptr;
    }
  } else {
    spdlog::error("shapeType not found or is of incorrect type");
    return nullptr;
  }

  // Based on the type_, create the corresponding shape
  switch (type) {
    case ShapeType::Plane:
      return std::make_unique<shapes::Plane>(mapData);
    case ShapeType::Cube:
      return std::make_unique<shapes::Cube>(mapData);
    case ShapeType::Sphere:
      return std::make_unique<shapes::Sphere>(mapData);
    default:
      // Handle unknown shape type
      spdlog::error("Unknown shape type: {}", static_cast<int32_t>(type));
      return nullptr;
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::addShapesToScene(
    std::vector<std::unique_ptr<BaseShape>>* shapes) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  // TODO remove this, just debug info print for now;
  /*for (auto& shape : *shapes) {
    shape->DebugPrint("Add shapes to scene");
  }*/

  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "addShapesToScene");
  const auto engine = filamentSystem->getFilamentEngine();

  filament::Engine* poFilamentEngine = engine;
  filament::Scene* poFilamentScene = filamentSystem->getFilamentScene();
  utils::EntityManager& oEntitymanager = poFilamentEngine->getEntityManager();
  // Ideally this is changed to create all entities on the first go, then
  // we pass them through, upon use this failed in filament engine, more R&D
  // needed
  // oEntitymanager.create(shapes.size(), lstEntities);

  for (auto& shape : *shapes) {
    auto oEntity = std::make_shared<Entity>(oEntitymanager.create());

    shape->bInitAndCreateShape(poFilamentEngine, oEntity);

    poFilamentScene->addEntity(*oEntity);

    // To investigate a better system for implementing layer mask
    // across dart to here.
    // auto& rcm = poFilamentEngine->getRenderableManager();
    // auto instance = rcm.getInstance(*oEntity.get());
    // To investigate
    // rcm.setLayerMask(instance, 0xff, 0x00);

    shapes_.emplace_back(shape.release());
  }

  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::vInitSystem() {
  vRegisterMessageHandler(
      ECSMessageType::ToggleShapesInScene, [this](const ECSMessage& msg) {
        spdlog::debug("ToggleShapesInScene");

        const auto value =
            msg.getData<bool>(ECSMessageType::ToggleShapesInScene);

        vToggleAllShapesInScene(value);

        spdlog::debug("ToggleShapesInScene Complete");
      });
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::vShutdownSystem() {
  // remove all filament entities.
  vRemoveAllShapesInScene();
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::DebugPrint() {
  SPDLOG_DEBUG("{} {}", __FILE__, __FUNCTION__);
}
}  // namespace plugin_filament_view
