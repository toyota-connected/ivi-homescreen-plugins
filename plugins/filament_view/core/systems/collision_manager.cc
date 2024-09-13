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
#include "collision_manager.h"

#include <core/entity/shapes/cube.h>
#include <viewer/custom_model_viewer.h>

#include "plugins/common/common.h"

namespace plugin_filament_view {

CollisionManager::CollisionManager() {}

CollisionManager* CollisionManager::m_poInstance = nullptr;
CollisionManager* CollisionManager::Instance() {
  if (m_poInstance == nullptr) {
    m_poInstance = new CollisionManager();
  }

  return m_poInstance;
}

void CollisionManager::vAddCollidable(EntityObject* collidable) {
  if (!collidable->HasComponentByStaticTypeID(Collidable::StaticGetTypeID())) {
    spdlog::error(
        "You tried to add an entityObject that didnt have a collidable on it, "
        "bailing out.");
    return;
  }

  collidables_.push_back(collidable);

  // this will eventually not be a shape, and be a model as well, for now just
  // shape.

  // make the BaseShape Object
  shapes::BaseShape* newShape = nullptr;  // new shapes::Cube();

  if (dynamic_cast<shapes::Cube*>(collidable)) {
    auto originalObject = dynamic_cast<shapes::Cube*>(collidable);
    newShape = new shapes::Cube();
    originalObject->CloneToOther(*dynamic_cast<shapes::BaseShape*>(newShape));
    newShape->m_bIsWireframe = true;

    newShape->DebugPrint("NewShape");
    originalObject->DebugPrint("OriginalShape");

    auto cmv = CustomModelViewer::Instance(__FUNCTION__);

    filament::Engine* poFilamentEngine = cmv->getFilamentEngine();
    filament::Scene* poFilamentScene = cmv->getFilamentScene();
    utils::EntityManager& oEntitymanager = poFilamentEngine->getEntityManager();

    auto oEntity = std::make_shared<utils::Entity>(oEntitymanager.create());

    newShape->bInitAndCreateShape(cmv->getFilamentEngine(), oEntity, nullptr);
    poFilamentScene->addEntity(*oEntity);

    // now store in map.
    collidablesDebugDrawingRepresentation_.insert(
        std::pair(originalObject->GetGlobalGuid(), newShape));
  }

  if (newShape == nullptr) {
    // log not handled;
    return;
  }

  /*//new
  switch (type) {
      case ShapeType::Plane:
          return std::make_unique<shapes::Plane>(flutter_assets_path, mapData);
      case ShapeType::Cube:
          return std::make_unique<shapes::Cube>(flutter_assets_path, mapData);
      case ShapeType::Sphere:
          return std::make_unique<shapes::Sphere>(flutter_assets_path, mapData);
      default:
          // Handle unknown shape type
              spdlog::error("Unknown shape type: {}",
  static_cast<int32_t>(type)); return nullptr;
  }*/
}

void CollisionManager::vRemoveCollidable(EntityObject* collidable) {
  collidables_.remove(collidable);
}

void CollisionManager::DebugPrint() {
  spdlog::debug("CollisionManager Debug Info:");
}

}  // namespace plugin_filament_view
