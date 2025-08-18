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

#include <encodable_value.h>

#include <core/components/derived/collider.h>
#include <core/components/derived/commonrenderable.h>
#include <core/components/derived/transform.h>
#include <core/entity/base/entityobject.h>
#include <core/utils/bounding_volumes.h>

namespace plugin_filament_view {

class MaterialSystem;
class ModelSystem;

/// TODO: remove this and refactor specific methods into the Renderable
/// component (CoI, come on)

// Renderable Entity Objects are intended to have material settings on them
// where NonRenderable EntityObjects do not. Its expected on play Renderable's
// have the ability to show up in the scene as models/shapes/objects
// where NonRenderables are more data without a physical representation
// NonRenderables are great for items like 'Global Light', Camera, hidden
// collision
class RenderableEntityObject : public EntityObject {
    friend class MaterialSystem;
    friend class ModelSystem;

  public:
    RenderableEntityObject()
      : EntityObject() {}
    explicit RenderableEntityObject(const flutter::EncodableMap& params)
      : EntityObject(params) {}

    explicit RenderableEntityObject(const std::string& name)
      : EntityObject(name) {}

    explicit RenderableEntityObject(const EntityGUID guid)
      : EntityObject(guid) {}
    explicit RenderableEntityObject(const std::string& name, const EntityGUID guid)
      : EntityObject(name, guid) {}

    virtual void debugPrint() const override {};

    void onInitialize() override;

    /*
     * Deserialization
     */
    virtual void deserializeFrom(const flutter::EncodableMap& params) override;

  public:
    /// TODO: use those in the addCollidable rewrite
    /// @returns The AABB of the entity
    [[nodiscard]] virtual AABB getAABB() const;

    /// The default implementation just returns a sphere with the max radius
    /// covering the AABB
    /// @returns The radius of the bounding sphere
    [[nodiscard]] inline BoundingSphere getBoundingSphere() const {
      return BoundingSphere(getAABB());
    }
};
}  // namespace plugin_filament_view
