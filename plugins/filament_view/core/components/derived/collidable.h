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
#include <core/include/shapetypes.h>
#include <core/scene/geometry/ray.h>

namespace plugin_filament_view {

// At the time of checkin - m_bShouldMatchAttachedObject is expected to be
// true at all times, and the IsStatic is not used in the false sense of
// updating geometry. First pass is only static collidables spawning using data
// from the base transform with on overrides. Expected work TBD for future
// improvements.

class Collidable : public Component {
 public:
  Collidable()
      : Component(std::string(__FUNCTION__)),
        m_bIsStatic(true),
        m_f3CenterPosition({0.0f, 0.0f, 0.0f}),
        m_nCollisionLayer(0),
        m_nCollisionMask(0xFFFFFFFF),
        m_bShouldMatchAttachedObject(false),
        m_eShapeType(),
        m_f3ExtentsSize({0.0f, 0.0f, 0.0f}) {}

  explicit Collidable(const flutter::EncodableMap& params);

  // Getters
  [[nodiscard]] bool GetIsStatic() const { return m_bIsStatic; }
  [[nodiscard]] int64_t GetCollisionLayer() const { return m_nCollisionLayer; }
  [[nodiscard]] int64_t GetCollisionMask() const { return m_nCollisionMask; }
  [[nodiscard]] bool GetShouldMatchAttachedObject() const {
    return m_bShouldMatchAttachedObject;
  }
  [[nodiscard]] ShapeType GetShapeType() const { return m_eShapeType; }
  [[nodiscard]] filament::math::float3 GetExtentsSize() const {
    return m_f3ExtentsSize;
  }
  [[nodiscard]] filament::math::float3 GetCenterPoint() const {
    return m_f3CenterPosition;
  }

  // Setters
  void SetIsStatic(bool value) { m_bIsStatic = value; }
  void SetCollisionLayer(int64_t value) { m_nCollisionLayer = value; }
  void SetCollisionMask(int64_t value) { m_nCollisionMask = value; }
  void SetShouldMatchAttachedObject(bool value) {
    m_bShouldMatchAttachedObject = value;
  }
  void SetShapeType(ShapeType value) { m_eShapeType = value; }
  void SetExtentsSize(const filament::math::float3& value) {
    m_f3ExtentsSize = value;
  }
  void SetCenterPoint(const filament::math::float3& value) {
    m_f3CenterPosition = value;
  }

  void DebugPrint(const std::string& tabPrefix) const override;

  [[nodiscard]] bool bDoesOverlap(const Collidable& other) const;
  bool bDoesIntersect(const Ray& ray,
                      ::filament::math::float3& hitPosition) const;

  static size_t StaticGetTypeID() { return typeid(Collidable).hash_code(); }

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] Component* Clone() const override {
    return new Collidable(*this);  // Copy constructor is called here
  }

 private:
  // If true, the object is static and won't sync move with its renderable
  // object once created in place.
  bool m_bIsStatic = true;
  // if this isStatic, then we need to copy this on creation
  // from basetransform property
  filament::math::float3 m_f3CenterPosition;

  // Layer for collision filtering
  // Not actively used in first iteration, but should be in future.
  int64_t m_nCollisionLayer = 0;
  int64_t m_nCollisionMask = 0xFFFFFFFF;

  // This works hand in hand with shapeType_, upon initialization if this is
  // true it will do its best to match the shape object it was sent in with from
  // Native. else it will use shapeType_ and extents;
  //
  // At the time of implementation, models must do their own shapeType_ usage.
  bool m_bShouldMatchAttachedObject = false;

  // if this !shouldMatchAttachedObject, then we need to deserialize these two
  // vars
  ShapeType m_eShapeType;
  filament::math::float3 m_f3ExtentsSize;
};

}  // namespace plugin_filament_view