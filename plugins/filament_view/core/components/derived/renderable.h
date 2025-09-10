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
#include <core/utils/bounding_volumes.h>
#include <core/utils/filament_types.h>

namespace plugin_filament_view {

class ModelSystem;
class ShapeSystem;

using FilamentRenderableInstance = utils::EntityInstance<filament::RenderableManager>;

class Renderable : public Component {
    friend class ModelSystem;
    friend class ShapeSystem;

  public:
    /// @brief Whether it should be culled when outside the camera frustum.
    bool allowCulling;
    /// @brief Whether it receives shadows.
    bool receiveShadows;
    /// @brief Whether it casts shadows.
    bool castShadows;
    /// TODO: Whether it casts contact shadows.
    // bool castContactShadows;
    /// TODO: Whether it's affected by fog.
    // bool affectedByFog;

    // Constructor
    Renderable()
      : Component(std::string(__FUNCTION__)),
        allowCulling(true),
        receiveShadows(true),
        castShadows(true)  //  ,
    // castContactShadows(false),
    // affectedByFog(true)
    {}
    explicit Renderable(const flutter::EncodableMap& params);

    Renderable(const Renderable& other)
      : Component(std::string(__FUNCTION__)),
        allowCulling(other.allowCulling),
        receiveShadows(other.receiveShadows),
        castShadows(other.castShadows)  // ,
    // castContactShadows(other.castContactShadows),
    // affectedByFog(other.affectedByFog)
    {}

    FilamentRenderableInstance _fInstance;

    void debugPrint(const std::string& tabPrefix) const override;

    [[nodiscard]] inline Component* Clone() const override {
      return new Renderable(*this);  // Copy constructor is called here
    }

    /// TODO: use those in the addCollidable rewrite
    /// @returns The AABB of the entity
    [[nodiscard]] virtual AABB getAABB() const;

    /// The default implementation just returns a sphere with the max radius
    /// covering the AABB
    /// @returns The radius of the bounding sphere
    [[nodiscard]] inline BoundingSphere getBoundingSphere() const {
      return BoundingSphere(getAABB());
    }

  protected:
    AABB _aabb;
};

}  // namespace plugin_filament_view
