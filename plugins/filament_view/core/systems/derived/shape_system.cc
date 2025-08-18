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

#include <core/components/derived/collider.h>

#include <core/entity/derived/shapes/baseshape.h>
#include <core/entity/derived/shapes/cube.h>
#include <core/entity/derived/shapes/plane.h>
#include <core/entity/derived/shapes/sphere.h>
#include <core/systems/ecs.h>
#include <plugins/common/common.h>

#include "collision_system.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::ToggleAllShapesInScene(const bool enable) const {
  if (enable) {
    for (const auto& guid : _shapes) {
      const auto shape = getShape(guid);
      shape->AddEntityToScene();
    }
  } else {
    for (const auto& guid : _shapes) {
      const auto shape = getShape(guid);
      shape->RemoveEntityFromScene();
    }
  }
}

bool ShapeSystem::hasShape(const EntityGUID guid) const {
  if (std::find(_shapes.begin(), _shapes.end(), guid) != _shapes.end()) {
    return true;
  } else {
    return false;
  }
}

BaseShape* ShapeSystem::getShape(const EntityGUID guid) const {
  if (hasShape(guid)) {
    return dynamic_cast<BaseShape*>(ecs->getEntity(guid).get());
  }
  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::ToggleSingleShapeInScene(const EntityGUID guid, const bool enable) const {
  if (!hasShape(guid)) {
    return;
  }

  BaseShape* shape = getShape(guid);
  if (enable) {
    shape->AddEntityToScene();
  } else {
    shape->RemoveEntityFromScene();
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::RemoveAllShapesInScene() {
  ToggleAllShapesInScene(false);

  for (const auto& guid : _shapes) {
    ecs->removeEntity(guid);
  }

  _shapes.clear();
}

////////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<BaseShape> ShapeSystem::poDeserializeShapeFromData(
  const flutter::EncodableMap& mapData
) {
  ShapeType type;

  // Find the "shapeType" key in the mapData
  if (const auto it = mapData.find(flutter::EncodableValue("shapeType"));
      it != mapData.end() && std::holds_alternative<int32_t>(it->second)) {
    // Check if the value is within the valid range of the ShapeType enum
    if (int32_t typeValue = std::get<int32_t>(it->second);
        typeValue > static_cast<int32_t>(ShapeType::Unset)
        && typeValue < static_cast<int32_t>(ShapeType::Max)) {
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
    case ShapeType::Plane: {
      auto toReturn = std::make_unique<Plane>();
      toReturn->deserializeFrom(mapData);
      return toReturn;
    }
    case ShapeType::Cube: {
      auto toReturn = std::make_unique<Cube>();
      toReturn->deserializeFrom(mapData);
      return toReturn;
    }
    case ShapeType::Sphere: {
      auto toReturn = std::make_unique<Sphere>();
      toReturn->deserializeFrom(mapData);
      return toReturn;
    }
    default:
      // Handle unknown shape type
      spdlog::error("Unknown shape type: {}", static_cast<int32_t>(type));
      return nullptr;
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::addShapesToScene(std::vector<std::shared_ptr<BaseShape>>* shapes) {
  SPDLOG_TRACE("++{}", __FUNCTION__);

  // TODO remove this, just debug info print for now;
  /*for (auto& shape : *shapes) {
    shape->debugPrint("Add shapes to scene");
  }*/

  for (auto& shape : *shapes) {
    addShapeToScene(shape);
  }

  SPDLOG_TRACE("--{}", __FUNCTION__);
}

void ShapeSystem::addShapeToScene(const std::shared_ptr<BaseShape>& shape) {
  runtime_assert(shape != nullptr, "ShapeSystem::addShapeToScene: shape cannot be null");

  filament::Scene* filamentScene = _filament->getFilamentScene();

  spdlog::trace("addShapesToScene: {}", shape->getGuid());
  FilamentEntity oEntity = _em->create();
  filamentScene->addEntity(oEntity);
  shape->_fEntity = oEntity;

  shape->bInitAndCreateShape(_engine, oEntity);

  spdlog::trace("Adding entity {} with filament entity {}", shape->getGuid(), oEntity.getId());

  // To investigate a better system for implementing layer mask
  // across dart to here.
  // auto instance = _rcm.getInstance(*oEntity.get());
  // To investigate
  // _rcm.setLayerMask(instance, 0xff, 0x00);

  _shapes.emplace_back(shape->getGuid());
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::onSystemInit() {
  // Get filament
  _filament = ecs->getSystem<FilamentSystem>(__FUNCTION__);
  runtime_assert(_filament != nullptr, "ModelSystem::onSystemInit: FilamentSystem not init yet");

  _engine = _filament->getFilamentEngine();
  runtime_assert(_engine != nullptr, "ModelSystem::onSystemInit: FilamentEngine not found");

  _rcm = _engine->getRenderableManager();
  _tm = _engine->getTransformManager();
  _em = _engine->getEntityManager();
  runtime_assert(_rcm != nullptr, "ModelSystem::onSystemInit: RenderableManager not found");
  runtime_assert(_tm != nullptr, "ModelSystem::onSystemInit: TransformManager not found");
  runtime_assert(_em != nullptr, "ModelSystem::onSystemInit: EntityManager not found");

  /*
   * Register message handlers
   */
  registerMessageHandler(ECSMessageType::ToggleShapesInScene, [this](const ECSMessage& msg) {
    spdlog::debug("ToggleShapesInScene");

    const auto value = msg.getData<bool>(ECSMessageType::ToggleShapesInScene);

    ToggleAllShapesInScene(value);

    spdlog::trace("ToggleShapesInScene Complete");
  });

  registerMessageHandler(ECSMessageType::SetShapeTransform, [this](const ECSMessage& msg) {
    SPDLOG_TRACE("SetShapeTransform");

    const auto guid = msg.getData<EntityGUID>(ECSMessageType::SetShapeTransform);

    const auto position = msg.getData<filament::math::float3>(ECSMessageType::Position);
    const auto rotation = msg.getData<filament::math::quatf>(ECSMessageType::Rotation);
    const auto scale = msg.getData<filament::math::float3>(ECSMessageType::Scale);

    // find the entity in our list:
    if (hasShape(guid)) {
      const auto entity = getShape(guid);
      const auto transform = entity->getComponent<Transform>();
      const auto collider = entity->getComponent<Collider>();

      transform->setTransform(position, scale, rotation);
    }

    SPDLOG_TRACE("SetShapeTransform Complete");
  });

  registerMessageHandler(ECSMessageType::ToggleVisualForEntity, [this](const ECSMessage& msg) {
    spdlog::debug("ToggleVisualForEntity");

    const auto guid = msg.getData<EntityGUID>(ECSMessageType::ToggleVisualForEntity);
    const auto value = msg.getData<bool>(ECSMessageType::BoolValue);

    ToggleSingleShapeInScene(guid, value);

    spdlog::trace("ToggleVisualForEntity Complete");
  });
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::update(double /*deltaTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::onDestroy() {
  // remove all filament entities.
  RemoveAllShapesInScene();
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::debugPrint() { SPDLOG_DEBUG("{}", __FUNCTION__); }
}  // namespace plugin_filament_view
