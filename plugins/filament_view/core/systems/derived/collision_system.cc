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
#include "collision_system.h"

#include "filament_system.h"

#include <core/entity/derived/model/model.h>
#include <core/entity/derived/shapes/cube.h>
#include <core/entity/derived/shapes/plane.h>
#include <core/entity/derived/shapes/sphere.h>
#include <core/systems/derived/shape_system.h>
#include <core/systems/derived/transform_system.h>
#include <core/systems/ecs.h>
#include <core/utils/asserts.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

static const TypeID COLLIDER_ID = IdentifiableType::StaticGetTypeID<Collider>();

/////////////////////////////////////////////////////////////////////////////////////////
flutter::EncodableValue HitResult::Encode() const {
  // Convert float3 to a list of floats
  flutter::EncodableList hitPosition = {
    flutter::EncodableValue(hitPosition_.x), flutter::EncodableValue(hitPosition_.y),
    flutter::EncodableValue(hitPosition_.z)
  };

  // Create a map to represent the HitResult
  flutter::EncodableMap encodableMap = {
    {flutter::EncodableValue("guid"), flutter::EncodableValue(guid_)},
    {flutter::EncodableValue("name"), flutter::EncodableValue(name_)},
    {flutter::EncodableValue("hitPosition"), flutter::EncodableValue(hitPosition)}
  };

  return flutter::EncodableValue(encodableMap);
}

/////////////////////////////////////////////////////////////////////////////////////////
void CollisionSystem::TurnOnRenderingOfCollidables() const {
  const auto colliders = ecs->getComponentsOfType<Collider>();
  for (const auto& collider : colliders) {
    const auto wireframe = collider->_wireframe;
    if (!!wireframe) {
      wireframe->AddEntityToScene();
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void CollisionSystem::TurnOffRenderingOfCollidables() const {
  const auto colliders = ecs->getComponentsOfType<Collider>();
  for (const auto& collider : colliders) {
    const auto wireframe = collider->_wireframe;
    if (!!wireframe) {
      wireframe->RemoveEntityFromScene();
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void CollisionSystem::debugPrint() {
  spdlog::debug("{}", __FUNCTION__);

  /*for (auto& collider : colliders) {
    collider->debugPrint();
  }*/
}

/////////////////////////////////////////////////////////////////////////////////////////
inline float fLength2(const filament::math::float3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

/////////////////////////////////////////////////////////////////////////////////////////
std::list<HitResult> CollisionSystem::
  lstCheckForCollidable(Ray& rayCast, int64_t /*collisionLayer*/) const {
  std::list<HitResult> hitResults;

  const auto colliders = ecs->getEntitiesWithComponent<Collider>();

  // Iterate over all entities.
  for (const auto& entity : colliders) {
    const EntityGUID guid = entity->getGuid();
    auto collider = ecs->getComponent<Collider>(guid);
    debug_assert(!!collider, ((fmt::format("Collider missing for entity: {}", guid)).data()));
    if (!collider->enabled) continue;

    // Check if the collision layer matches (if a specific layer was provided)
    // if (collisionLayer != 0 && (collider->getCollisionLayer() &
    // collisionLayer) == 0) {
    //    continue; // Skip if layers don't match
    // }

    const auto transform = ecs->getComponent<Transform>(guid);

    // Perform intersection test with the ray
    if (filament::math::float3 hitLocation; collider->intersects(rayCast, hitLocation, transform)) {
      // If there is an intersection, create a HitResult
      HitResult hitResult;
      hitResult.guid_ = guid;
      hitResult.name_ = collider->eventName;
      hitResult.hitPosition_ = hitLocation;  // Set the hit location

      SPDLOG_INFO("HIT RESULT: {}", hitResult.guid_);

      // Add to the hit results
      hitResults.push_back(hitResult);
    }
  }

  // Sort hit results by distance from the ray's origin
  hitResults.sort([&rayCast](const HitResult& a, const HitResult& b) {
    // Calculate the squared distance to avoid the cost of sqrt
    const auto distanceA = fLength2(a.hitPosition_ - rayCast.f3GetPosition());
    const auto distanceB = fLength2(b.hitPosition_ - rayCast.f3GetPosition());

    // Sort in ascending order (closest hit first)
    return distanceA < distanceB;
  });

  // Return the sorted list of hit results
  return hitResults;
}

/////////////////////////////////////////////////////////////////////////////////////////
void CollisionSystem::SendCollisionInformationCallback(
  const std::list<HitResult>& lstHitResults,
  std::string sourceQuery,
  const CollisionEventType eType
) const {
  flutter::EncodableMap encodableMap;

  // event type
  encodableMap[flutter::EncodableValue(kCollisionEventType)] = static_cast<int>(eType);
  // source guid
  encodableMap[flutter::EncodableValue(kCollisionEventSourceGuid)] = sourceQuery;
  // hit count
  encodableMap[flutter::EncodableValue(kCollisionEventHitCount)] = static_cast<int>(
    lstHitResults.size()
  );

  int iter = 0;
  for (const auto& arg : lstHitResults) {
    std::ostringstream oss;
    oss << kCollisionEventHitResult << iter;

    encodableMap[flutter::EncodableValue(oss.str())] = arg.Encode();

    ++iter;
  }

  SendDataToEventChannel(encodableMap);
}

/////////////////////////////////////////////////////////////////////////////////////////
void CollisionSystem::onSystemInit() {
  // Register component listeners
  ecs->registerComponentListener(
    *this,  //
    COLLIDER_ID
  );

  /*
   * Register message handlers
   */
  registerMessageHandler(ECSMessageType::CollisionRequest, [this](const ECSMessage& msg) {
    auto rayInfo = msg.getData<Ray>(ECSMessageType::CollisionRequest);
    const auto requestor = msg.getData<std::string>(ECSMessageType::CollisionRequestRequestor);
    const auto type = msg.getData<CollisionEventType>(ECSMessageType::CollisionRequestType);

    const auto hitList = lstCheckForCollidable(rayInfo, 0);

    SendCollisionInformationCallback(hitList, requestor, type);
  });

  registerMessageHandler(
    ECSMessageType::ToggleDebugCollidableViewsInScene,
    [this](const ECSMessage& msg) {
      spdlog::debug("ToggleDebugCollidableViewsInScene");

      const auto value = msg.getData<bool>(ECSMessageType::ToggleDebugCollidableViewsInScene);

      if (!value) {
        TurnOffRenderingOfCollidables();
      } else {
        TurnOnRenderingOfCollidables();
      }

      spdlog::debug("ToggleDebugCollidableViewsInScene Complete");
    }
  );

  registerMessageHandler(ECSMessageType::ToggleCollisionForEntity, [this](const ECSMessage& msg) {
    const auto guid = msg.getData<EntityGUID>(ECSMessageType::ToggleCollisionForEntity);
    const auto value = msg.getData<bool>(ECSMessageType::BoolValue);

    if (const auto collider = ecs->getComponent<Collider>(guid); !!collider) {
      collider->enabled = value;
    }
  });
}

void CollisionSystem::onComponentOperation(
  EntityObject& entity,
  Component& component,
  ECSOperation operation
) {
  if (component.getTypeID() != IdentifiableType::StaticGetTypeID<Collider>()) {
    return;
  }

  auto collider = static_cast<Collider&>(component);

  // Handle component operations specific to the collision system
  switch (operation) {
    case ECSOperation::Add:
      _addCollider(entity, collider);
      break;
    case ECSOperation::Remove:
      _removeCollider(entity, collider);
      break;
  }
}

void CollisionSystem::_addCollider(EntityObject& entity, Collider& collider) {
  spdlog::debug("CollisionSystem: Added collider to entity({})", entity.getGuid());

  AABB& aabb = collider.aabb;

  // Make sure it has an AABB
  if (aabb.isEmpty()) {
    spdlog::debug("Collider entity({}) has no AABB", entity.getGuid());

    // Get AABB if it's a RenderableEntityObject
    if (const auto renderableEntity = dynamic_cast<RenderableEntityObject*>(&entity)) {
      spdlog::debug("  Adding AABB to collider entity({})", renderableEntity->getGuid());
      collider.aabb = renderableEntity->getAABB();

      spdlog::debug(
        "  AABB.pos: x={}, y={}, z={}",  //
        collider.aabb.center.x, collider.aabb.center.y, collider.aabb.center.z
      );
      spdlog::debug(
        "  AABB.size: x={}, y={}, z={}",  //
        collider.aabb.halfExtent.x * 2,   //
        collider.aabb.halfExtent.y * 2,   //
        collider.aabb.halfExtent.z * 2    //
      );
#if SPDLOG_LEVEL == trace
// renderableEntity->getComponent<Transform>()->debugPrint("  ");
#endif
    } else {
      spdlog::error("  Collider owner does not have an AABB");
    }
  }

  // Create a cube wireframe
  spdlog::debug("Creating wireframe for collider entity({})", entity.getGuid());
  auto cubeChild = std::make_shared<shapes::Cube>("(collider wireframe)");
  cubeChild->m_bIsWireframe = true;
  cubeChild->addComponent<MaterialDefinitions>(kDefaultMaterial);
  const auto shapeSystem = ecs->getSystem<ShapeSystem>("CollisionSystem::update");
  ecs->addEntity(cubeChild);
  shapeSystem->addShapeToScene(cubeChild);
  auto childTransform = cubeChild->getComponent<Transform>();
  childTransform->setTransform(aabb.center, aabb.halfExtent * 2, kQuatfIdentity);

  childTransform->setParent(entity.getGuid());

  collider._wireframe = cubeChild;
}

void CollisionSystem::_removeCollider(EntityObject& entity, Collider& collider) {
  spdlog::debug("CollisionSystem: Removed collider from entity({})", entity.getGuid());

  // Remove wireframe
  if (collider._wireframe) {
    ecs->removeEntity(collider._wireframe->getGuid());
    collider._wireframe.reset();
  }
}

void CollisionSystem::update(double /*deltaTime*/) {}

/////////////////////////////////////////////////////////////////////////////////////////
void CollisionSystem::onDestroy() {}

}  // namespace plugin_filament_view
