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

#include <filament/Engine.h>
#include <filament/math/mat4.h>
#include <filament/math/quat.h>
#include <filament/math/vec3.h>
#include <utils/Entity.h>
#include <memory>

#include <gltfio/FilamentAsset.h>
#include "core/components/derived/basetransform.h"

namespace plugin_filament_view {

using ::utils::Entity;

class EntityTransforms {
 public:
  // Utility functions for quaternion to matrix conversion
  static filament::math::mat3f QuaternionToMat3f(
      const filament::math::quatf& rotation);
  static filament::math::mat4f QuaternionToMat4f(
      const filament::math::quatf& rotation);

  // Utility functions to create identity matrices
  static filament::math::mat3f identity3x3();
  static filament::math::mat4f identity4x4();

  // Utility function to create a shear matrix and apply it to a mat4f
  static filament::math::mat4f oApplyShear(
      const filament::math::mat4f& matrix,
      const filament::math::float3 shear);  // NOLINT

  // Static functions that use CustomModelViewer to get the engine
  static void vApplyScale(const std::shared_ptr<utils::Entity>& poEntity,
                          const filament::math::float3 scale);  // NOLINT
  static void vApplyRotation(const std::shared_ptr<utils::Entity>& poEntity,
                             const filament::math::quatf rotation);  // NOLINT
  static void vApplyTranslate(
      const std::shared_ptr<utils::Entity>& poEntity,
      const filament::math::float3 translation);  // NOLINT
  static void vApplyTransform(const std::shared_ptr<utils::Entity>& poEntity,
                              const filament::math::mat4f& transform);
  static void vApplyTransform(const std::shared_ptr<utils::Entity>& poEntity,
                              filament::math::quatf rotation,
                              filament::math::float3 scale,
                              filament::math::float3 translation);
  static void vApplyShear(const std::shared_ptr<utils::Entity>& poEntity,
                          const filament::math::float3 shear);  // NOLINT
  static void vResetTransform(const std::shared_ptr<utils::Entity>& poEntity);
  static filament::math::mat4f oGetCurrentTransform(
      const std::shared_ptr<utils::Entity>& poEntity);
  static void vApplyLookAt(const std::shared_ptr<utils::Entity>& poEntity,
                           filament::math::float3 target,
                           filament::math::float3 up);

  // Static functions that take an engine as a parameter
  static void vApplyScale(const std::shared_ptr<utils::Entity>& poEntity,
                          const filament::math::float3 scale,  // NOLINT
                          ::filament::Engine* engine);
  static void vApplyRotation(const std::shared_ptr<utils::Entity>& poEntity,
                             const filament::math::quatf rotation,  // NOLINT
                             ::filament::Engine* engine);
  static void vApplyTranslate(
      const std::shared_ptr<utils::Entity>& poEntity,
      const filament::math::float3 translation,  // NOLINT
      ::filament::Engine* engine);
  static void vApplyTransform(const std::shared_ptr<utils::Entity>& poEntity,
                              const filament::math::mat4f& transform,
                              ::filament::Engine* engine);
  static void vApplyTransform(const std::shared_ptr<utils::Entity>& poEntity,
                              filament::math::quatf rotation,
                              filament::math::float3 scale,
                              filament::math::float3 translation,
                              ::filament::Engine* engine);
  static void vApplyShear(const std::shared_ptr<utils::Entity>& poEntity,
                          const filament::math::float3 shear,  // NOLINT
                          ::filament::Engine* engine);
  static void vResetTransform(const std::shared_ptr<utils::Entity>& poEntity,
                              ::filament::Engine* engine);
  static filament::math::mat4f oGetCurrentTransform(
      const std::shared_ptr<utils::Entity>& poEntity,
      ::filament::Engine* engine);
  static void vApplyLookAt(const std::shared_ptr<utils::Entity>& poEntity,
                           filament::math::float3 target,
                           filament::math::float3 up,
                           ::filament::Engine* engine);
  static void vApplyTransform(filament::gltfio::FilamentAsset* poAsset,
                              const BaseTransform& transform);
  static void vApplyTransform(filament::gltfio::FilamentAsset* poAsset,
                              const BaseTransform& transform,
                              ::filament::Engine* engine);
};

}  // namespace plugin_filament_view
