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

#include "component.h"
#include "core/include/shapetypes.h"
#include "core/scene/geometry/ray.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

namespace plugin_filament_view {

class Collidable : public Component {
 public:
  Collidable()
      : Component(std::string(__FUNCTION__)),
        isStatic(true),
        m_f3CenterPosition({0.0f, 0.0f, 0.0f}),
        collisionLayer(0),
        collisionMask(0xFFFFFFFF),
        shouldMatchAttachedObject(false),
        shapeType_(),
        m_f3ExtentsSize({0.0f, 0.0f, 0.0f}) {}

  explicit Collidable(const flutter::EncodableMap& params);

  // Getters
  [[nodiscard]] bool GetIsStatic() const { return isStatic; }
  [[nodiscard]] int64_t GetCollisionLayer() const { return collisionLayer; }
  [[nodiscard]] int64_t GetCollisionMask() const { return collisionMask; }
  [[nodiscard]] bool GetShouldMatchAttachedObject() const {
    return shouldMatchAttachedObject;
  }
  [[nodiscard]] ShapeType GetShapeType() const { return shapeType_; }
  [[nodiscard]] filament::math::float3 GetExtentsSize() const {
    return m_f3ExtentsSize;
  }
  [[nodiscard]] filament::math::float3 GetCenterPoint() const {
    return m_f3CenterPosition;
  }

  // Setters
  void SetIsStatic(bool value) { isStatic = value; }
  void SetCollisionLayer(int64_t value) { collisionLayer = value; }
  void SetCollisionMask(int64_t value) { collisionMask = value; }
  void SetShouldMatchAttachedObject(bool value) {
    shouldMatchAttachedObject = value;
  }
  void SetShapeType(ShapeType value) { shapeType_ = value; }
  void SetExtentsSize(const filament::math::float3& value) {
    m_f3ExtentsSize = value;
  }
  void SetCenterPoint(const filament::math::float3& value) {
    m_f3CenterPosition = value;
  }

  void DebugPrint(const std::string& tabPrefix) const override;

  bool bDoesOverlap(const Collidable& other) const;
  bool bDoesIntersect(const Ray& ray, ::filament::math::float3 &hitPosition) const;

  static size_t StaticGetTypeID() { return typeid(Collidable).hash_code(); }

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  Component* Clone() const override {
    return new Collidable(*this);  // Copy constructor is called here
  }

 private:
  // If true, the object is static and won't sync move with its renderable
  // object once created in place.
  bool isStatic = true;
  // if this isStatic, then we need to copy this on creation
  // from basetransform property
  filament::math::float3 m_f3CenterPosition;

  // Layer for collision filtering
  // Not actively used in first iteration, but should be in future.
  int64_t collisionLayer = 0;
  int64_t collisionMask = 0xFFFFFFFF;

  // This works hand in hand with shapeType_, upon initialization if this is
  // true it will do its best to match the shape object it was sent in with from
  // Native. else it will use shapeType_ and extents;
  //
  // At the time of implementation, models must do their own shapeType_ usage.
  bool shouldMatchAttachedObject = false;

  // if this !shouldMatchAttachedObject, then we need to deserialize these two
  // vars
  ShapeType shapeType_;
  filament::math::float3 m_f3ExtentsSize;
};

}  // namespace plugin_filament_view