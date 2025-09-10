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

#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/math/mat3.h>
#include <filament/math/norm.h>
#include <filament/math/vec3.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

using filament::Aabb;
using filament::IndexBuffer;
using filament::RenderableManager;
using filament::VertexAttribute;
using filament::VertexBuffer;
using filament::math::float3;
using filament::math::mat3f;
using filament::math::packSnorm16;
using filament::math::short4;

typedef struct MeshData {
    VertexBuffer* vertexBuffer;
    IndexBuffer* indexBuffer;
};

static constexpr int kDefaultSphereDivisions = 24;

class ShapeUtils {
  public:
    /// @brief Creates a cube shape and returns the vertex and index buffers.
    static MeshData CreateCube(::filament::Engine* engine);

    /// @brief Creates a plane shape and returns the vertex and index buffers.
    static MeshData CreatePlane(::filament::Engine* engine);

    /// @brief Creates a sphere shape and returns the vertex and index buffers.
    static MeshData CreateSphere(
      ::filament::Engine* engine,
      int stacks = kDefaultSphereDivisions,
      int slices = kDefaultSphereDivisions
    );
}

}  // namespace plugin_filament_view
