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

#include "core/shapes/baseshape.h"

namespace plugin_filament_view {

class MaterialManager;
using ::utils::Entity;

namespace shapes {

class Sphere : public BaseShape {
 public:
  Sphere(const std::string& flutter_assets_path,
         const flutter::EncodableMap& params);
  ~Sphere() override = default;

  // Disallow copy and assign.
  Sphere(const Sphere&) = delete;
  Sphere& operator=(const Sphere&) = delete;

  void DebugPrint(const char* tag) const override;

  bool bInitAndCreateShape(::filament::Engine* engine_,
                           std::shared_ptr<Entity> entityObject,
                           MaterialManager* material_manager) override;

 private:
  static void createDoubleSidedSphere(::filament::Engine* engine_,
                                      MaterialManager* material_manager);

  void createSingleSidedSphere(::filament::Engine* engine_,
                               MaterialManager* material_manager);

  int stacks_;
  int slices_;

  std::vector<::filament::math::float3> vertices;
  std::vector<::filament::math::float3> normals;
  std::vector<unsigned short> indices;
};

}  // namespace shapes
}  // namespace plugin_filament_view
