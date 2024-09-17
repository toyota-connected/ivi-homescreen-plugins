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

#include <list>
#include "core/components/basetransform.h"
#include "core/components/collidable.h"
#include "core/entity/shapes/baseshape.h"

namespace plugin_filament_view {

class HitResult {
 public:
 EntityGUID guid_;
 std::string name_;
 ::filament::math::float3 hitPosition_;
};

// Ideally this is replaced by a physics engine eventually that has a scenegraph
// or spatial tree structure in place that makes this type of work more
// efficient.
class CollisionManager {
 public:
  CollisionManager();

  void vCleanup();
  void DebugPrint();

  // Disallow copy and assign.
  CollisionManager(const CollisionManager&) = delete;
  CollisionManager& operator=(const CollisionManager&) = delete;

  void vTurnOnRenderingOfCollidables();
  void vTurnOffRenderingOfCollidables();

  void vUpdate();

  void vAddCollidable(EntityObject* collidable);
  void vRemoveCollidable(EntityObject* collidable);

  // send in your ray, get a list of hit results back, collisionLayer not actively used - future work.
  std::list<HitResult> lstCheckForCollidable(Ray& rayCast, int64_t collisionLayer = 0) const;

  // Checks to see if we already has this guid in our mapping.
  bool bHasEntityObjectRepresentation(EntityGUID guid) const;

  static CollisionManager* Instance();

 private:
  static CollisionManager* m_poInstance;

  bool currentlyDrawingDebugCollidables = false;

  void vMatchCollidablesToRenderingModelsTransforms();
  void vMatchCollidablesToDebugDrawingTransforms();

  std::list<EntityObject*> collidables_;
  std::map<EntityGUID, shapes::BaseShape*>
      collidablesDebugDrawingRepresentation_;
};

}  // namespace plugin_filament_view
