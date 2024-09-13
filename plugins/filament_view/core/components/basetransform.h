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

#include <filament/math/quat.h>
#include "component.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

namespace plugin_filament_view {

class BaseTransform : public Component {
 public:
  // Constructor
  BaseTransform()
      : Component(std::string(__FUNCTION__)),
        m_f3CenterPosition(0, 0, 0),
        m_f3ExtentsSize(0, 0, 0),
        m_f3Scale(1, 1, 1),
        m_quatRotation(0, 0, 0, 1) {}
  explicit BaseTransform(const flutter::EncodableMap& params);

  // Getters
  [[nodiscard]] const filament::math::float3& GetCenterPosition() const {
    return m_f3CenterPosition;
  }

  [[nodiscard]] const filament::math::float3& GetExtentsSize() const {
    return m_f3ExtentsSize;
  }

  [[nodiscard]] const filament::math::float3& GetScale() const {
    return m_f3Scale;
  }

  [[nodiscard]] const filament::math::quatf& GetRotation() const {
    return m_quatRotation;
  }

  // Setters
  void SetCenterPosition(const filament::math::float3& centerPosition) {
    m_f3CenterPosition = centerPosition;
  }

  void SetExtentsSize(const filament::math::float3& extentsSize) {
    m_f3ExtentsSize = extentsSize;
  }

  void SetScale(const filament::math::float3& scale) { m_f3Scale = scale; }

  void SetRotation(const filament::math::quatf& rotation) {
    m_quatRotation = rotation;
  }

  void DebugPrint(const std::string& tabPrefix) const override;

  static size_t StaticGetTypeID() { return typeid(BaseTransform).hash_code(); }

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  Component* Clone() const override {
    return new BaseTransform(*this);  // Copy constructor is called here
  }

 private:
  filament::math::float3 m_f3CenterPosition;
  filament::math::float3 m_f3ExtentsSize;
  filament::math::float3 m_f3Scale;
  filament::math::quatf m_quatRotation;
};

}  // namespace plugin_filament_view