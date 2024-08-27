/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include "shape_manager.h"

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/View.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>
#include "asio/post.hpp"

#include "baseshape.h"
#include "cube.h"
#include "plane.h"
#include "plugins/common/common.h"
#include "sphere.h"

namespace plugin_filament_view {

using ::filament::Aabb;
using ::filament::IndexBuffer;
using ::filament::RenderableManager;
using ::filament::VertexAttribute;
using ::filament::VertexBuffer;
using ::filament::math::float3;
using ::filament::math::mat3f;
using ::filament::math::mat4f;
using ::filament::math::packSnorm16;
using ::filament::math::short4;
using shapes::BaseShape;
using ::utils::Entity;

ShapeManager::ShapeManager(MaterialManager* material_manager)
    : material_manager_(material_manager) {
  SPDLOG_TRACE("++ShapeManager::ShapeManager");
  SPDLOG_TRACE("--ShapeManager::ShapeManager");
}

ShapeManager::~ShapeManager() {
  // remove all filament entities.
  vRemoveAllShapesInScene();
}

void ShapeManager::vToggleAllShapesInScene(bool bValue) {
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

void ShapeManager::vRemoveAllShapesInScene() {
  vToggleAllShapesInScene(false);
  shapes_.clear();
}

std::unique_ptr<BaseShape> ShapeManager::poDeserializeShapeFromData(
    const std::string& flutter_assets_path,
    const flutter::EncodableMap& mapData) {
  shapes::BaseShape::ShapeType type;

  // Find the "shapeType" key in the mapData
  auto it = mapData.find(flutter::EncodableValue("shapeType"));
  if (it != mapData.end() && std::holds_alternative<int32_t>(it->second)) {
    int32_t typeValue = std::get<int32_t>(it->second);

    // Check if the value is within the valid range of the ShapeType enum
    if (typeValue > static_cast<int32_t>(shapes::BaseShape::ShapeType::Unset) &&
        typeValue < static_cast<int32_t>(shapes::BaseShape::ShapeType::Max)) {
      type = static_cast<shapes::BaseShape::ShapeType>(typeValue);
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
    case shapes::BaseShape::ShapeType::Plane:
      return std::make_unique<shapes::Plane>(flutter_assets_path, mapData);
    case shapes::BaseShape::ShapeType::Cube:
      return std::make_unique<shapes::Cube>(flutter_assets_path, mapData);
    case shapes::BaseShape::ShapeType::Sphere:
      return std::make_unique<shapes::Sphere>(flutter_assets_path, mapData);
    default:
      // Handle unknown shape type
      spdlog::error("Unknown shape type: {}", static_cast<int32_t>(type));
      return nullptr;
  }
}

void ShapeManager::addShapesToScene(
    std::vector<std::unique_ptr<BaseShape>>* shapes) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  filament::Engine* poFilamentEngine =
      CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
  filament::Scene* poFilamentScene =
      CustomModelViewer::Instance(__FUNCTION__)->getFilamentScene();
  utils::EntityManager& oEntitymanager = poFilamentEngine->getEntityManager();
  // oEntitymanager.create(shapes.size(), lstEntities);

  for (auto& shape : *shapes) {
    auto oEntity = std::make_shared<utils::Entity>(oEntitymanager.create());

    shape->bInitAndCreateShape(poFilamentEngine, oEntity, material_manager_);

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

}  // namespace plugin_filament_view
