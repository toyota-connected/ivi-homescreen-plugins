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

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <core/components/base/component.h>
#include <core/components/derived/transform.h>
#include <core/include/shapetypes.h>
#include <core/scene/geometry/ray.h>
#include <core/utils/bounding_volumes.h>

namespace plugin_filament_view {

// At the time of checkin - m_bShouldMatchAttachedObject is expected to be
// true at all times, and the IsStatic is not used in the false sense of
// updating geometry. First pass is only static colliders spawning using data
// from the base transform with on overrides. Expected work TBD for future
// improvements.

namespace shapes {
class BaseShape;
}  // namespace shapes

class CollisionSystem;

class Collider : public Component {
    friend class CollisionSystem;

  public:
    // You can turn collision objects on / off during runtime without removing /
    // re-adding from the scene.
    bool enabled = true;
    /// @brief The name of the event to be triggered on collision.
    /// This is used to identify the event in the event system.
    /// The default value is "click".
    std::string eventName = "click";
    /// Bounding box of the collider
    AABB aabb;

  public:
    Collider()
      : Component(std::string(__FUNCTION__)) {}

    // TODO: make this a more complete constructor
    Collider(const ShapeType& shapeType)
      : Component(std::string(__FUNCTION__)),
        m_eShapeType(shapeType) {}

    explicit Collider(const flutter::EncodableMap& params);

    // Getters
    [[nodiscard]] inline bool getIsStatic() const { return m_bIsStatic; }
    [[nodiscard]] inline int64_t getCollisionLayer() const { return m_nCollisionLayer; }
    [[nodiscard]] inline int64_t getCollisionMask() const { return m_nCollisionMask; }
    [[nodiscard]] inline bool getShouldMatchAttachedObject() const {
      return m_bShouldMatchAttachedObject;
    }
    [[nodiscard]] inline ShapeType getShapeType() const { return m_eShapeType; }

    // Setters
    inline void setIsStatic(bool value) { m_bIsStatic = value; }
    inline void setCollisionLayer(int64_t value) { m_nCollisionLayer = value; }
    inline void setCollisionMask(int64_t value) { m_nCollisionMask = value; }
    inline void setShouldMatchAttachedObject(bool value) { m_bShouldMatchAttachedObject = value; }
    inline void setShapeType(ShapeType value) { m_eShapeType = value; }

    void debugPrint(const std::string& tabPrefix) const override;

    [[nodiscard]] bool bDoesOverlap(const Collider& other) const;
    bool intersects(
      const Ray& ray,
      ::filament::math::float3& hitPosition,
      const std::shared_ptr<Transform>& transform
    ) const;

    [[nodiscard]] inline Component* Clone() const override {
      return new Collider(*this);  // Copy constructor is called here
    }

  private:
    // If true, the object is static and won't sync move with its renderable
    // object once created in place.
    bool m_bIsStatic = false;
    // if this isStatic, then we need to copy this on creation
    // from transform property
    filament::math::float3 m_f3StaticPosition = {0, 0, 0};

    // Layer for collision filtering
    // Not actively used in first iteration, but should be in future.
    int64_t m_nCollisionLayer = 0;
    int64_t m_nCollisionMask = 0xFFFFFFFF;

    // This works hand in hand with shapeType_, upon initialization if this is
    // true it will do its best to match the shape object it was sent in with from
    // Native. else it will use shapeType_ and extents;
    //
    // At the time of implementation, models must do their own shapeType_ usage.
    bool m_bShouldMatchAttachedObject = true;

    /// Collider shape type (supported values: Cube)
    /// TODO: add support for other shapes
    ShapeType m_eShapeType = ShapeType::Cube;  // default
    /// @deprecated
    /// TODO: remove
    filament::math::float3 _extentSize = {1, 1, 1};  // default

    /*
     *  Setup stuff
     */
    /// Collider's child wireframe object
    std::shared_ptr<shapes::BaseShape> _wireframe;
};

}  // namespace plugin_filament_view
