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

#include <math/vec3.h>

#include "direction.h"
#include "position.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

namespace plugin_filament_view {

class Ray {
 public:
  Ray(Position& pos, Direction& dir, float length)
      : position_(pos), direction_(dir), length_(length) {}
  explicit Ray(const flutter::EncodableMap& params);

  [[nodiscard]] ::filament::math::float3 f3GetPosition() const {
    return position_;
  }

  [[nodiscard]] ::filament::math::float3 f3GetDirection() const {
    return direction_;
  }

  [[nodiscard]] double dGetLength() const { return length_; }

  void DebugPrint(const char* tag);

 private:
  Direction direction_;
  Position position_;
  double length_;
};

}  // namespace plugin_filament_view
