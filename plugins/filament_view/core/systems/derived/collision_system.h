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

#include <core/components/derived/collider.h>
#include <core/entity/derived/shapes/baseshape.h>
#include <core/include/literals.h>
#include <core/systems/base/system.h>
#include <flutter_desktop_plugin_registrar.h>
#include <list>

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
class CollisionSystem : public System {
    friend class ModelSystem;

  public:
    CollisionSystem() = default;

    void Cleanup();
    void debugPrint() override;

    // Disallow copy and assign.
    CollisionSystem(const CollisionSystem&) = delete;
    CollisionSystem& operator=(const CollisionSystem&) = delete;

    void TurnOnRenderingOfCollidables() const;
    void TurnOffRenderingOfCollidables() const;

    void update(double deltaTime) override;

    void onSystemInit() override;
    void onDestroy() override;

    void onComponentOperation(
      EntityObject& entity,  //
      Component& component,
      ECSOperation operation
    ) override;

    // send in your ray, get a list of hit results back, collisionLayer not
    // actively used - future work.
    std::list<HitResult> lstCheckForCollidable(Ray& rayCast, int64_t collisionLayer = 0) const;

    // this will send the hit information sent in to non-native (Dart) code.
    void SendCollisionInformationCallback(
      const std::list<HitResult>& lstHitResults,
      std::string sourceQuery,
      CollisionEventType eType
    ) const;

  private:
    bool currentlyDrawingDebugCollidables = false;

    void MatchCollidablesToRenderingModelsTransforms();
    void MatchCollidablesToDebugDrawingTransforms();

    void _addCollider(EntityObject& entity, Collider& collider);
    void _removeCollider(EntityObject& entity, Collider& collider);
};

}  // namespace plugin_filament_view
