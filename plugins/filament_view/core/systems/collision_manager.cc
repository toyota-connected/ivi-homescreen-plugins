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
#include <core/entity/shapes/plane.h>
#include <core/entity/shapes/sphere.h>
#include <viewer/custom_model_viewer.h>

#include "plugins/common/common.h"

namespace plugin_filament_view {

flutter::EncodableValue HitResult::Encode() const {
  // Convert float3 to a list of floats
  flutter::EncodableList hitPosition = {
      flutter::EncodableValue(hitPosition_.x),
      flutter::EncodableValue(hitPosition_.y),
      flutter::EncodableValue(hitPosition_.z)};

  // Create a map to represent the HitResult
  flutter::EncodableMap encodableMap = {
      {flutter::EncodableValue("guid"), flutter::EncodableValue(guid_)},
      {flutter::EncodableValue("name"), flutter::EncodableValue(name_)},
      {flutter::EncodableValue("hitPosition"),
       flutter::EncodableValue(hitPosition)}};

  return flutter::EncodableValue(encodableMap);
}

CollisionManager::CollisionManager() {}

CollisionManager* CollisionManager::m_poInstance = nullptr;
CollisionManager* CollisionManager::Instance() {
  if (m_poInstance == nullptr) {
    m_poInstance = new CollisionManager();
  }

  return m_poInstance;
}

bool CollisionManager::bHasEntityObjectRepresentation(EntityGUID guid) const {
  return collidablesDebugDrawingRepresentation_.find(guid) !=
         collidablesDebugDrawingRepresentation_.end();
}

void CollisionManager::vAddCollidable(EntityObject* collidable) {
  if (!collidable->HasComponentByStaticTypeID(Collidable::StaticGetTypeID())) {
    spdlog::error(
        "You tried to add an entityObject that didnt have a collidable on it, "
        "bailing out.");
    return;
  }

  auto originalCollidable = dynamic_cast<Collidable*>(
      collidable->GetComponentByStaticTypeID(Collidable::StaticGetTypeID())
          .get());
  if (originalCollidable != nullptr &&
      originalCollidable->GetShouldMatchAttachedObject()) {
    auto originalShape = dynamic_cast<shapes::BaseShape*>(collidable);
    SPDLOG_WARN("ORIGINAL SHAPE");
    if (originalShape != nullptr) {
      originalCollidable->SetShapeType(originalShape->type_);
      originalCollidable->SetExtentsSize(
          originalShape->m_poBaseTransform.lock()->GetExtentsSize());
      originalCollidable->DebugPrint("Collidable push back");
    }
  }

  collidables_.push_back(collidable);

  // make the BaseShape Object
  shapes::BaseShape* newShape = nullptr;
  if (dynamic_cast<Model*>(collidable)) {
    auto ourModelObject = dynamic_cast<Model*>(collidable);
    auto ourAABB = ourModelObject->getAsset()->getBoundingBox();

    newShape = new shapes::Cube();
    newShape->m_bDoubleSided = false;
    newShape->type_ = ShapeType::Cube;

    ourModelObject->vShallowCopyComponentToOther(
        BaseTransform::StaticGetTypeID(), *newShape);
    ourModelObject->vShallowCopyComponentToOther(
        CommonRenderable::StaticGetTypeID(), *newShape);

    std::shared_ptr<Component> componentBT =
        newShape->GetComponentByStaticTypeID(BaseTransform::StaticGetTypeID());
    std::shared_ptr<BaseTransform> baseTransformPtr =
        std::dynamic_pointer_cast<BaseTransform>(componentBT);

    std::shared_ptr<Component> componentCR =
        newShape->GetComponentByStaticTypeID(
            CommonRenderable::StaticGetTypeID());
    std::shared_ptr<CommonRenderable> commonRenderablePtr =
        std::dynamic_pointer_cast<CommonRenderable>(componentCR);

    newShape->m_poBaseTransform =
        std::weak_ptr<BaseTransform>(baseTransformPtr);
    newShape->m_poCommonRenderable =
        std::weak_ptr<CommonRenderable>(commonRenderablePtr);

    auto ourTransform = baseTransformPtr;

    // SPDLOG_WARN("TEMP A {} {} {}", ourAABB.center().x, ourAABB.center().y,
    // ourAABB.center().z); SPDLOG_WARN("TEMP B {} {} {}",
    // ourTransform->GetCenterPosition().x, ourTransform->GetCenterPosition().y,
    // ourTransform->GetCenterPosition().z);

    // Note i believe this is correct; more thorough testing is needed; there's
    // a concern around exporting models not centered at 0,0,0 and not being
    // 100% accurate.
    ourTransform->SetCenterPosition(ourAABB.center() +
                                    ourTransform->GetCenterPosition());
    ourTransform->SetExtentsSize(ourAABB.extent());
    ourTransform->SetScale(ourAABB.extent());

  } else if (dynamic_cast<shapes::Cube*>(collidable)) {
    auto originalObject = dynamic_cast<shapes::Cube*>(collidable);
    newShape = new shapes::Cube();
    originalObject->CloneToOther(*dynamic_cast<shapes::BaseShape*>(newShape));
  } else if (dynamic_cast<shapes::Sphere*>(collidable)) {
    auto originalObject = dynamic_cast<shapes::Sphere*>(collidable);
    newShape = new shapes::Sphere();
    originalObject->CloneToOther(*dynamic_cast<shapes::BaseShape*>(newShape));
  } else if (dynamic_cast<shapes::Plane*>(collidable)) {
    auto originalObject = dynamic_cast<shapes::Plane*>(collidable);
    newShape = new shapes::Plane();
    originalObject->CloneToOther(*dynamic_cast<shapes::BaseShape*>(newShape));
    newShape->DebugPrint("new Plane\t");
  }

  if (newShape == nullptr) {
    // log not handled;
    spdlog::error("Failed to create collidable shape.");
    return;
  }

  newShape->m_bIsWireframe = true;

  auto cmv = CustomModelViewer::Instance(__FUNCTION__);

  filament::Engine* poFilamentEngine = cmv->getFilamentEngine();
  filament::Scene* poFilamentScene = cmv->getFilamentScene();
  utils::EntityManager& oEntitymanager = poFilamentEngine->getEntityManager();

  auto oEntity = std::make_shared<utils::Entity>(oEntitymanager.create());

  newShape->bInitAndCreateShape(cmv->getFilamentEngine(), oEntity, nullptr);
  poFilamentScene->addEntity(*oEntity);

  // now store in map.
  collidablesDebugDrawingRepresentation_.insert(
      std::pair(collidable->GetGlobalGuid(), newShape));
}

void CollisionManager::vRemoveCollidable(EntityObject* collidable) {
  collidables_.remove(collidable);

  // Remove from collidablesDebugDrawingRepresentation_
  auto iter =
      collidablesDebugDrawingRepresentation_.find(collidable->GetGlobalGuid());
  if (iter != collidablesDebugDrawingRepresentation_.end()) {
    delete iter->second;
    collidablesDebugDrawingRepresentation_.erase(iter);
  }
}

void CollisionManager::vTurnOnRenderingOfCollidables() {
  for (auto& collidable : collidablesDebugDrawingRepresentation_) {
    collidable.second->vRemoveEntityFromScene();
  }
}

void CollisionManager::vTurnOffRenderingOfCollidables() {
  for (auto& collidable : collidablesDebugDrawingRepresentation_) {
    collidable.second->vAddEntityToScene();
  }
}

void CollisionManager::DebugPrint() {
  spdlog::debug("CollisionManager Debug Info:");
}

inline float fLength2(const filament::math::float3& v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

std::list<HitResult> CollisionManager::lstCheckForCollidable(
    Ray& rayCast,
    int64_t /*collisionLayer*/) const {
  std::list<HitResult> hitResults;

  // Iterate over all entities.
  for (const auto& entity : collidables_) {
    // Make sure collidable is still here....
    auto collidable = std::dynamic_pointer_cast<Collidable>(
        entity->GetComponentByStaticTypeID(Collidable::StaticGetTypeID()));
    if (!collidable) {
      continue;  // No collidable component, skip this entity
    }

    // Check if the collision layer matches (if a specific layer was provided)
    // if (collisionLayer != 0 && (collidable->GetCollisionLayer() &
    // collisionLayer) == 0) {
    //    continue; // Skip if layers don't match
    // }

    // Variable to hold the hit position
    filament::math::float3 hitLocation;

    // Perform intersection test with the ray
    if (collidable->bDoesIntersect(rayCast, hitLocation)) {
      // If there is an intersection, create a HitResult
      HitResult hitResult;
      hitResult.guid_ = entity->GetGlobalGuid();
      hitResult.name_ = entity->GetName();
      hitResult.hitPosition_ = hitLocation;  // Set the hit location

      SPDLOG_WARN("HIT RESULT: {}", hitResult.guid_);

      // Add to the hit results
      hitResults.push_back(hitResult);
    }
  }

  // Sort hit results by distance from the ray's origin
  hitResults.sort([&rayCast](const HitResult& a, const HitResult& b) {
    // Calculate the squared distance to avoid the cost of sqrt
    auto distanceA = fLength2(a.hitPosition_ - rayCast.f3GetPosition());
    auto distanceB = fLength2(b.hitPosition_ - rayCast.f3GetPosition());

    // Sort in ascending order (closest hit first)
    return distanceA < distanceB;
  });

  // Return the sorted list of hit results
  return hitResults;
}

void CollisionManager::setupMessageChannels(
    flutter::PluginRegistrar* plugin_registrar) {
  const std::string channel_name =
      std::string("plugin.filament_view.collision_info");

  collisionInfoCallback_ = std::make_unique<flutter::MethodChannel<>>(
      plugin_registrar->messenger(), channel_name,
      &flutter::StandardMethodCodec::GetInstance());
}

void CollisionManager::SendCollisionInformationCallback(
    std::list<HitResult>& lstHitResults,
    std::string sourceQuery,
    CollisionEventType eType) const {
  if (collisionInfoCallback_ == nullptr) {
    return;
  }

  flutter::EncodableMap encodableMap;

  // event type
  encodableMap[flutter::EncodableValue(kCollisionEventType)] =
      static_cast<int>(eType);
  // source guid
  encodableMap[flutter::EncodableValue(kCollisionEventSourceGuid)] =
      sourceQuery;
  // hit count
  encodableMap[flutter::EncodableValue(kCollisionEventHitCount)] =
      static_cast<int>(lstHitResults.size());

  int iter = 0;
  for (const auto& arg : lstHitResults) {
    std::ostringstream oss;
    oss << kCollisionEventHitResult << iter;

    encodableMap[flutter::EncodableValue(oss.str())] = arg.Encode();

    ++iter;
  }
  collisionInfoCallback_->InvokeMethod(
      kCollisionEvent, std::make_unique<flutter::EncodableValue>(
                           flutter::EncodableValue(encodableMap)));
}

}  // namespace plugin_filament_view
