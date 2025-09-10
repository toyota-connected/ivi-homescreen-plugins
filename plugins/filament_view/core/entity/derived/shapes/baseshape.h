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

#include <core/components/derived/collider.h>
#include <core/components/derived/renderable.h>
#include <core/components/derived/shape.h>
#include <core/components/derived/transform.h>
#include <core/entity/base/entityobject.h>
#include <core/scene/geometry/direction.h>
#include <core/systems/derived/material_system.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>
#include <utils/EntityManager.h>

namespace plugin_filament_view {

class CollisionSystem;
class ShapeSystem;
class ModelSystem;

class BaseShape : public EntityObject {
    friend class plugin_filament_view::CollisionSystem;
    friend class plugin_filament_view::ShapeSystem;
    friend class plugin_filament_view::ModelSystem;

  public:
    /// @brief Constructor for BaseShape. Generates a GUID and has an empty
    /// name.
    BaseShape(ShapeType type)
      : EntityObject(),
        type_(type) {}
    /// @brief Constructor for BaseShape with a name. Generates a unique GUID.
    explicit BaseShape(std::string name, ShapeType type)
      : EntityObject(name),
        type_(type) {}
    /// @brief Constructor for BaseShape with GUID. Name is empty.
    explicit BaseShape(EntityGUID guid, ShapeType type)
      : EntityObject(guid),
        type_(type) {}
    /// @brief Constructor for BaseShape with a name and GUID.
    BaseShape(std::string name, EntityGUID guid, ShapeType type)
      : EntityObject(name, guid),
        type_(type) {}

    ~BaseShape() override;

    virtual void debugPrint(const char* tag) const;

    // Disallow copy and assign.
    BaseShape(const BaseShape&) = delete;
    BaseShape& operator=(const BaseShape&) = delete;

    // will copy over properties, but not 'create' anything.
    // similar to a shallow copy.
    virtual void CloneToOther(BaseShape& other) const;

    virtual bool bInitAndCreateShape(::filament::Engine* engine_) = 0;

    void RemoveEntityFromScene() const;
    void AddEntityToScene() const;

  protected:
    ::filament::VertexBuffer* m_poVertexBuffer = nullptr;
    ::filament::IndexBuffer* m_poIndexBuffer = nullptr;

    virtual void deserializeFrom(const flutter::EncodableMap& params) override;

    void onInitialize() override;

    void debugPrint() const override;

    // uses Vertex and Index buffer to create the material and geometry
    // using all the internal variables.
    void BuildRenderable(::filament::Engine* engine_);

    ShapeType type_ = ShapeType::Unset;

    /// direction of the shape rotation in the world space
    filament::math::float3 m_f3Normal = filament::math::float3(0, 0, 0);

    bool _hasRenderable = false;

  private:
    void DestroyBuffers();

    // This does NOT come over as a property (currently), only used by
    // CollisionManager when created debug wireframe models for seeing collider
    // shapes.
    bool m_bIsWireframe = false;
};

}  // namespace plugin_filament_view
