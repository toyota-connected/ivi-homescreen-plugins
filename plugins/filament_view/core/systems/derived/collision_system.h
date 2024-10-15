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

#include <flutter_desktop_plugin_registrar.h>
#include <list>
#include <core/components/derived/collidable.h>
#include <core/entity/derived/shapes/baseshape.h>
#include <core/include/literals.h>
#include <core/systems/base/ecsystem.h>

namespace plugin_filament_view {

class HitResult {
 public:
  EntityGUID guid_;
  std::string name_;
  ::filament::math::float3 hitPosition_;

  [[nodiscard]] flutter::EncodableValue Encode() const;
};

// Ideally this is replaced by a physics engine eventually that has a scenegraph
// or spatial tree structure in place that makes this type of work more
// efficient.
class CollisionSystem : public ECSystem {
 public:
  CollisionSystem() = default;

  void vCleanup();
  void DebugPrint() override;

  // Disallow copy and assign.
  CollisionSystem(const CollisionSystem&) = delete;
  CollisionSystem& operator=(const CollisionSystem&) = delete;

  void vTurnOnRenderingOfCollidables();
  void vTurnOffRenderingOfCollidables();

  void vUpdate(float fElapsedTime) override;

  void vInitSystem() override;
  void vShutdownSystem() override;

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(CollisionSystem).hash_code();
  }

  void vAddCollidable(EntityObject* collidable);
  void vRemoveCollidable(EntityObject* collidable);

  void setupMessageChannels(flutter::PluginRegistrar* plugin_registrar);

  // send in your ray, get a list of hit results back, collisionLayer not
  // actively used - future work.
  std::list<HitResult> lstCheckForCollidable(Ray& rayCast,
                                             int64_t collisionLayer = 0) const;

  // this will send the hit information sent in to non-native (Dart) code.
  void SendCollisionInformationCallback(std::list<HitResult>& lstHitResults,
                                        std::string sourceQuery,
                                        CollisionEventType eType) const;

  // Checks to see if we already has this guid in our mapping.
  [[nodiscard]] bool bHasEntityObjectRepresentation(
      const EntityGUID& guid) const;

 private:
  bool currentlyDrawingDebugCollidables = false;

  void vMatchCollidablesToRenderingModelsTransforms();
  void vMatchCollidablesToDebugDrawingTransforms();

  // Used for sending messages back over to Dart for hitResults.
  std::unique_ptr<flutter::MethodChannel<>> collisionInfoCallback_;

  std::list<EntityObject*> collidables_;
  std::map<EntityGUID, shapes::BaseShape*>
      collidablesDebugDrawingRepresentation_;
};

}  // namespace plugin_filament_view
