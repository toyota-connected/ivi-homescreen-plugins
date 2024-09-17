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

    auto ourTransform = dynamic_cast<BaseTransform*>(
        newShape->GetComponentByStaticTypeID(BaseTransform::StaticGetTypeID()));

    // SPDLOG_WARN("TEMP A {} {} {}", ourAABB.center().x, ourAABB.center().y, ourAABB.center().z);
    // SPDLOG_WARN("TEMP B {} {} {}", ourTransform->GetCenterPosition().x, ourTransform->GetCenterPosition().y, ourTransform->GetCenterPosition().z);

    // Note i believe this is correct; more thorough testing is needed; there's a concern
    // around exporting models not centered at 0,0,0 and not being 100% accurate.
    ourTransform->SetCenterPosition(ourAABB.center() + ourTransform->GetCenterPosition());
    ourTransform->SetExtentsSize(ourAABB.extent());
    ourTransform->SetScale(ourAABB.extent());

    newShape->m_poBaseTransform = ourTransform;
    newShape->m_poCommonRenderable =
        dynamic_cast<CommonRenderable*>(newShape->GetComponentByStaticTypeID(
            CommonRenderable::StaticGetTypeID()));

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

}  // namespace plugin_filament_view
