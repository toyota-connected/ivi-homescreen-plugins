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

#include "baseshape.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

namespace plugin_filament_view {

class Sphere : public BaseShape {
  public:
    // /// @brief Constructor for Sphere. Generates a GUID and has an empty name.
    Sphere()
      : BaseShape(ShapeType::Sphere) {}
    /// @brief Constructor for Sphere with a name. Generates a unique GUID.
    explicit Sphere(std::string name)
      : BaseShape(name, ShapeType::Sphere) {}
    /// @brief Constructor for Sphere with GUID. Name is empty.
    explicit Sphere(EntityGUID guid)
      : BaseShape(guid, ShapeType::Sphere) {}
    /// @brief Constructor for Sphere with a name and GUID.
    explicit Sphere(std::string name, EntityGUID guid)
      : BaseShape(name, guid, ShapeType::Sphere) {}
    ~Sphere() override = default;

    // Disallow copy and assign.
    Sphere(const Sphere&) = delete;
    Sphere& operator=(const Sphere&) = delete;

    virtual void deserializeFrom(const flutter::EncodableMap& params) override;

    void debugPrint(const char* tag) const override;

    bool bInitAndCreateShape(::filament::Engine* engine_, FilamentEntity entityObject) override;
    void CloneToOther(BaseShape& other) const override;

  private:
    static void createDoubleSidedSphere(::filament::Engine* engine_);

    void createSingleSidedSphere(::filament::Engine* engine_);

    int stacks_ = 20;
    int slices_ = 20;

    std::vector<::filament::math::float3> vertices_;
    std::vector<::filament::math::float3> normals_;
    std::vector<unsigned short> indices_;
    std::vector<::filament::math::float2> uvs_;
};

}  // namespace plugin_filament_view
