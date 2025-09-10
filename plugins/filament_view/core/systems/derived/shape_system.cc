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

#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <math/norm.h>
#include <math/vec3.h>

#include <core/components/derived/collider.h>
#include <core/components/derived/shape.h>
#include <core/systems/derived/collision_system.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/derived/transform_system.h>
#include <core/systems/ecs.h>
#include <core/utils/deserialize.h>

#include <core/include/literals.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

using filament::Aabb;
using filament::IndexBuffer;
using filament::RenderableManager;
using filament::VertexAttribute;
using filament::VertexBuffer;
using filament::math::float3;
using filament::math::mat3f;
using filament::math::mat4f;
using filament::math::packSnorm16;
using filament::math::short4;

void ShapeSystem::ToggleAllShapesInScene(const bool enable) const {
  if (enable) {
    for (const auto& guid : _shapes) {
      const auto shape = ecs->getComponent<Shape>(guid);
      if (shape) {
        // shape->AddEntityToScene();
        spdlog::warn("[{}] Unimplemented!!! line {}", __FUNCTION__, __LINE__);
      }
    }
  } else {
    for (const auto& guid : _shapes) {
      const auto shape = ecs->getComponent<Shape>(guid);
      // shape->RemoveEntityFromScene();
      spdlog::warn("[{}] Unimplemented!!! line {}", __FUNCTION__, __LINE__);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::ToggleSingleShapeInScene(const EntityGUID guid, const bool enable) const {
  if (!hasShape(guid)) {
    return;
  }

  BaseShape* shape = ecs->getComponent<Shape>(guid);
  if (enable) {
    // shape->AddEntityToScene();
    spdlog::warn("[{}] Unimplemented!!! line {}", __FUNCTION__, __LINE__);
  } else {
    // shape->RemoveEntityFromScene();
    spdlog::warn("[{}] Unimplemented!!! line {}", __FUNCTION__, __LINE__);
  }
}

void ShapeSystem::onComponentOperation(
  EntityObject& entity,
  Component& component,
  const ECSOperation operation
) {
  if (component.getTypeID() != IdentifiableType::StaticGetTypeID<Shape>()) {
    return;
  }

  auto shape = static_cast<Shape&>(component);

  // Handle component operations specific to the shape system
  switch (operation) {
    case ECSOperation::Add:
      _addShape(entity, shape);
      break;
    case ECSOperation::Remove:
      _removeShape(entity, shape);
      break;
  }
}

void ShapeSystem::_addShape(EntityObject& entity, Shape& shape) {
  auto guid = entity.getGuid();
  spdlog::debug("ShapeSystem: Added shape to entity({})", guid);

  if (hasShape(guid)) {
    spdlog::warn("Entity({}) already has a shape registered in ShapeSystem, skipping", guid);
    return;
  }

  // Make sure it has a Material component
  const auto materialDefinitions = entity.getComponent<Material>();
  if (!materialDefinitions) {
    spdlog::warn("BaseShape({}) has no material, adding default material", guid);
    entity.addComponent(kDefaultMaterial);  // init with defaults
  }

  filament::Scene* filamentScene = _filament->getFilamentScene();

  spdlog::trace("addShapesToScene: {}", guid);
  FilamentEntity oEntity = _em->create();
  filamentScene->addEntity(oEntity);
  entity._fEntity = oEntity;

  spdlog::trace("Adding entity {} with filament entity {}", guid, oEntity.getId());

  // To investigate a better system for implementing layer mask
  // across dart to here.
  // auto instance = _rcm.getInstance(*oEntity.get());
  // To investigate
  // _rcm.setLayerMask(instance, 0xff, 0x00);

  _shapes.emplace_back(guid);
}

void ShapeSystem::_removeShape(EntityObject& entity, Shape& shape) {
  auto guid = entity.getGuid();
  spdlog::debug("ShapeSystem: Removed shape from entity({})", guid);

  if (!hasShape(guid)) {
    spdlog::warn("Entity({}) has no shape registered in ShapeSystem, skipping", guid);
    return;
  }

  // Remove from scene
  // shape.RemoveEntityFromScene();
    spdlog::warn("[{}] Unimplemented!!! line {}", __FUNCTION__, __LINE__);

  // Remove from internal list
  _shapes.erase(std::remove(_shapes.begin(), _shapes.end(), guid), _shapes.end());

  // Destroy filament entity
  if (entity._fEntity) {
    spdlog::trace("Removing entity {} with filament entity {}", guid, entity._fEntity.getId());
    _em->destroy(entity._fEntity);
    entity._fEntity.clear();
  } else {
    spdlog::warn("Entity {} has no filament entity to destroy", guid);
  }
}

////////////////////////////////////////////////////////////////////////////
void ShapeSystem::_buildRenderable(EntityObject& entity, Shape& shape) {
  const auto& name = entity.name;
  const auto guid = entity.getGuid();
  spdlog::debug("[{}] Building renderable for shape '{}'({})", __FUNCTION__, name, guid);
  // assertInitialized();
  // material_manager can and will be null for now on wireframe creation.

  auto* engine_ = _filament->getFilamentEngine();

  filament::math::float3 aabb;
  switch (shape.type) {
    case ShapeType::Cube:
    case ShapeType::Sphere:
      aabb = {0.5f, 0.5f, 0.5f};  // NOTE: faces forward by default
      break;
    case ShapeType::Plane:
      aabb = {0.5f, 0.5f, 0.005f};  // NOTE: faces sideways by default
      break;
    default:
      aabb = {0, 0, 0};
      spdlog::error("Unknown shape type: {}", static_cast<int>(shape.type));
      break;
  }

  spdlog::debug("[{}] Building shape '{}'({})", __FUNCTION__, name, guid);

  const auto transform = entity.getComponent<Transform>();

  spdlog::debug("[{}] AABB.scale: x={}, y={}, z={}", __FUNCTION__, aabb.x, aabb.y, aabb.z);

  spdlog::debug("Getting components...");
  // const auto material = getComponent<Material>();
  // spdlog::debug("Found Material component");
  // runtime_assert(!!material, "Missing Material component");
  // runtime_assert(!!material->_instance, "Material not instantiated");
  // runtime_assert(!!material->_instance->getData(), "Material instance has no data");

  // const auto renderable = entity.getComponent<Renderable>();
  // runtime_assert(!!renderable, "Missing Renderable component");
  // bool hasCulling = renderable->IsCullingOfObjectEnabled();
  // bool receiveShadows = renderable->IsReceiveShadowsEnabled();
  // bool castShadows = renderable->IsCastShadowsEnabled();

  bool hasCulling = false;
  bool receiveShadows = true;
  bool castShadows = true;

  spdlog::debug("[{}] Building renderable...", __FUNCTION__);

  if (shape.isWireframe) {
    // TODO: setup a wireframe material
    RenderableManager::Builder(1)
      .boundingBox({{}, aabb})  // center, halfExtent
      // .material(0, material->_instance->getData().value())
      .geometry(0, RenderableManager::PrimitiveType::LINES, shape._vertexBuffer, shape._indexBuffer)
      .culling(hasCulling)
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine_, _fEntity);
  } else {
    RenderableManager::Builder(1)
      .boundingBox({{}, aabb})
      // .material(0, material->_instance->getData().value())
      .geometry(
        0, RenderableManager::PrimitiveType::TRIANGLES, shape._vertexBuffer, shape._indexBuffer
      )
      .culling(hasCulling)
      .receiveShadows(receiveShadows)
      .castShadows(castShadows)
      .build(*engine_, entity._fEntity);
  }

  spdlog::debug("[{}] Built renderable", __FUNCTION__);

  transform->_fInstance = engine_->getTransformManager().getInstance(entity._fEntity);
  // renderable->_fInstance = engine_->getRenderableManager().getInstance(entity._fEntity);

  spdlog::debug("[{}] Initialized renderable", __FUNCTION__);

  // Get parent entity id
  const auto parentId = transform->getParentId();

  const auto transformSystem = ecs->getSystem<TransformSystem>("BaseShape::BuildRenderable");
  if (parentId != kNullGuid) {
    // Get the parent entity object
    auto parentEntity = ecs->getEntity(parentId);

    transform->setParent(parentEntity->getGuid());
  }

  /// NOTE: why is this needed? if this is not called the collider doesn't work,
  ///       even though it's visible
  transformSystem->applyTransform(guid, true);

  spdlog::debug("[{}] Applied transform to entity {}", __FUNCTION__, guid);

  // TODO , need 'its done building callback to delete internal arrays data'
  // - note the calls are async built, but doesn't seem to be a method internal
  // to filament for when the building is complete. Further R&D is needed.
}

////////////////////////////////////////////////////////////////////////////
void ShapeSystem::_hideShape(Shape& shape) const {
  auto entity = shape.getOwner()->_fEntity;
  if (!entity) {
    spdlog::warn("Attempt to remove uninitialized shape from scene {}", __FUNCTION__);
    return;
  }

  const auto filamentSystem = ecs->getSystem<FilamentSystem>("ShapeSystem::RemoveEntityFromScene");
  filamentSystem->getFilamentScene()->remove(entity);
}

////////////////////////////////////////////////////////////////////////////
void ShapeSystem::_showShape(Shape& shape) const {
  auto entity = shape.getOwner()->_fEntity;
  if (!entity) {
    spdlog::warn("Attempt to add uninitialized shape to scene {}", __FUNCTION__);
    return;
  }

  const auto filamentSystem = ecs->getSystem<FilamentSystem>("ShapeSystem::RemoveEntityFromScene");
  filamentSystem->getFilamentScene()->addEntity(entity);
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
      const auto entity = ecs->getComponent<Shape>(guid);
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
void ShapeSystem::update(double /*deltaTime*/) {
  // Hide disabled shapes
  const auto shapes = ecs->getComponentsOfType<Shape>();
  for (const auto& [guid, shape] : shapes) {
    if (!shape->enabled) {
      _hideShape(*shape);
    } else {
      _showShape(*shape);
    }
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::onDestroy() {
  // remove all filament entities.
  RemoveAllShapesInScene();
}

////////////////////////////////////////////////////////////////////////////////////
void ShapeSystem::debugPrint() { SPDLOG_DEBUG("{}", __FUNCTION__); }
}  // namespace plugin_filament_view
