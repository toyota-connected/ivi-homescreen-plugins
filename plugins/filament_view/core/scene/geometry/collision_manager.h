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
#include "core/shapes/baseshape.h"

namespace plugin_filament_view {

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

  /// GroupingGuid is how the objects recognize one another
  /// This should be the 'EntityObject' (parent) object
  void vAddCollidable(std::string groupingGuid,
                      Collidable* collidable,
                      BaseTransform* transform);

 private:
  bool currentlyDrawingDebugCollidables = false;

  void vMatchCollidablesToRenderingModelsTransforms();

  std::list<shapes::BaseShape*> collidablesDebugDrawingRepresentation_;
};

}  // namespace plugin_filament_view
