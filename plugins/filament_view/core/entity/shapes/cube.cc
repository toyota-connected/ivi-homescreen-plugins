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

#include "cube.h"

#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>

#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view::shapes {

using ::filament::Aabb;
using ::filament::IndexBuffer;
using ::filament::RenderableManager;
using ::filament::VertexAttribute;
using ::filament::VertexBuffer;
using ::filament::math::float3;
using ::filament::math::mat3f;
using ::filament::math::packSnorm16;
using ::filament::math::short4;
using ::utils::Entity;

Cube::Cube(const std::string& flutter_assets_path,
           const flutter::EncodableMap& params)
    : BaseShape(flutter_assets_path, params) {
  SPDLOG_TRACE("+-{} {}", __FILE__, __FUNCTION__);
}

bool Cube::bInitAndCreateShape(::filament::Engine* engine_,
                               std::shared_ptr<Entity> entityObject,
                               MaterialManager* material_manager) {
  m_poEntity = std::move(entityObject);

  if (m_bDoubleSided)
    createDoubleSidedCube(engine_, material_manager);
  else
    createSingleSidedCube(engine_, material_manager);
  return true;
}

void Cube::createDoubleSidedCube(::filament::Engine* engine_,
                                 MaterialManager* material_manager) {
  // Vertices for a cube (24 vertices for outside, 24 for inside)
  static const float vertices[] = {
      // Outside Front face
      -0.5f, -0.5f, 0.5f,  // Vertex 0
      0.5f, -0.5f, 0.5f,   // Vertex 1
      0.5f, 0.5f, 0.5f,    // Vertex 2
      -0.5f, 0.5f, 0.5f,   // Vertex 3

      // Outside Back face
      0.5f, -0.5f, -0.5f,   // Vertex 4
      -0.5f, -0.5f, -0.5f,  // Vertex 5
      -0.5f, 0.5f, -0.5f,   // Vertex 6
      0.5f, 0.5f, -0.5f,    // Vertex 7

      // Outside Right face
      0.5f, -0.5f, 0.5f,   // Vertex 8
      0.5f, -0.5f, -0.5f,  // Vertex 9
      0.5f, 0.5f, -0.5f,   // Vertex 10
      0.5f, 0.5f, 0.5f,    // Vertex 11

      // Outside Left face
      -0.5f, -0.5f, -0.5f,  // Vertex 12
      -0.5f, -0.5f, 0.5f,   // Vertex 13
      -0.5f, 0.5f, 0.5f,    // Vertex 14
      -0.5f, 0.5f, -0.5f,   // Vertex 15

      // Outside Top face
      -0.5f, 0.5f, 0.5f,   // Vertex 16
      0.5f, 0.5f, 0.5f,    // Vertex 17
      0.5f, 0.5f, -0.5f,   // Vertex 18
      -0.5f, 0.5f, -0.5f,  // Vertex 19

      // Outside Bottom face
      -0.5f, -0.5f, -0.5f,  // Vertex 20
      0.5f, -0.5f, -0.5f,   // Vertex 21
      0.5f, -0.5f, 0.5f,    // Vertex 22
      -0.5f, -0.5f, 0.5f,   // Vertex 23

      // Inside Front face (inverted)
      -0.5f, -0.5f, 0.5f,  // Vertex 24
      0.5f, -0.5f, 0.5f,   // Vertex 25
      0.5f, 0.5f, 0.5f,    // Vertex 26
      -0.5f, 0.5f, 0.5f,   // Vertex 27

      // Inside Back face (inverted)
      0.5f, -0.5f, -0.5f,   // Vertex 28
      -0.5f, -0.5f, -0.5f,  // Vertex 29
      -0.5f, 0.5f, -0.5f,   // Vertex 30
      0.5f, 0.5f, -0.5f,    // Vertex 31

      // Inside Right face (inverted)
      0.5f, -0.5f, 0.5f,   // Vertex 32
      0.5f, -0.5f, -0.5f,  // Vertex 33
      0.5f, 0.5f, -0.5f,   // Vertex 34
      0.5f, 0.5f, 0.5f,    // Vertex 35

      // Inside Left face (inverted)
      -0.5f, -0.5f, -0.5f,  // Vertex 36
      -0.5f, -0.5f, 0.5f,   // Vertex 37
      -0.5f, 0.5f, 0.5f,    // Vertex 38
      -0.5f, 0.5f, -0.5f,   // Vertex 39

      // Inside Top face (inverted)
      -0.5f, 0.5f, 0.5f,   // Vertex 40
      0.5f, 0.5f, 0.5f,    // Vertex 41
      0.5f, 0.5f, -0.5f,   // Vertex 42
      -0.5f, 0.5f, -0.5f,  // Vertex 43

      // Inside Bottom face (inverted)
      -0.5f, -0.5f, -0.5f,  // Vertex 44
      0.5f, -0.5f, -0.5f,   // Vertex 45
      0.5f, -0.5f, 0.5f,    // Vertex 46
      -0.5f, -0.5f, 0.5f    // Vertex 47
  };

  // UV coordinates for each face (same for inside and outside)
  static const float uvCoords[] = {
      // Outside Front face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Outside Back face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Outside Right face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Outside Left face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Outside Top face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Outside Bottom face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,

      // Inside Front face (same UVs as outside)
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Inside Back face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Inside Right face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Inside Left face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Inside Top face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
      // Inside Bottom face
      0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

  // Indices for double-sided cube (showing both inside and outside)
  static const uint16_t indices[] = {
      // Outside faces
      0, 1, 2, 0, 2, 3,        // Front
      4, 5, 6, 4, 6, 7,        // Back
      8, 9, 10, 8, 10, 11,     // Right
      12, 13, 14, 12, 14, 15,  // Left
      16, 17, 18, 16, 18, 19,  // Top
      20, 21, 22, 20, 22, 23,  // Bottom

      // Inside faces (inverted winding order)
      24, 27, 26, 24, 26, 25,  // Front
      28, 31, 30, 28, 30, 29,  // Back
      32, 35, 34, 32, 34, 33,  // Right
      36, 39, 38, 36, 38, 37,  // Left
      40, 43, 42, 40, 42, 41,  // Top
      44, 47, 46, 44, 46, 45   // Bottom
  };

  const static short4 normals[] = {
      // Outer Front face (Z+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),

      // Outer Back face (Z-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),

      // Outer Right face (X+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),

      // Outer Left face (X-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),

      // Outer Top face (Y+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),

      // Outer Bottom face (Y-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),

      // Inner Front face (Z-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),

      // Inner Back face (Z+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),

      // Inner Right face (X-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),

      // Inner Left face (X+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),

      // Inner Top face (Y-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),

      // Inner Bottom face (Y+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw)};

  m_poVertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(48)  // 24 vertices for outside, 24 for inside
          .bufferCount(3)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::TANGENTS, 1,
                     VertexBuffer::AttributeType::SHORT4)
          .attribute(VertexAttribute::UV0, 2,
                     VertexBuffer::AttributeType::FLOAT2)
          .normalized(VertexAttribute::TANGENTS)
          .build(*engine_);

  m_poVertexBuffer->setBufferAt(
      *engine_, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 2, VertexBuffer::BufferDescriptor(uvCoords, sizeof(uvCoords)));

  constexpr int indexCount =
      72 * 2;  // 24 triangles * 3 vertices (inside + outside)
  m_poIndexBuffer = IndexBuffer::Builder()
                        .indexCount(indexCount)
                        .bufferType(IndexBuffer::IndexType::USHORT)
                        .build(*engine_);

  m_poIndexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  vBuildRenderable(engine_, material_manager);
}

void Cube::createSingleSidedCube(::filament::Engine* engine_,
                                 MaterialManager* material_manager) {
  // Vertices for a cube (24 vertices, 4 per face)
  static const float vertices[] = {
      // Front face
      -0.5f, -0.5f, 0.5f,  // Vertex 0
      0.5f, -0.5f, 0.5f,   // Vertex 1
      0.5f, 0.5f, 0.5f,    // Vertex 2
      -0.5f, 0.5f, 0.5f,   // Vertex 3

      // Back face
      0.5f, -0.5f, -0.5f,   // Vertex 4
      -0.5f, -0.5f, -0.5f,  // Vertex 5
      -0.5f, 0.5f, -0.5f,   // Vertex 6
      0.5f, 0.5f, -0.5f,    // Vertex 7

      // Right face
      0.5f, -0.5f, 0.5f,   // Vertex 8
      0.5f, -0.5f, -0.5f,  // Vertex 9
      0.5f, 0.5f, -0.5f,   // Vertex 10
      0.5f, 0.5f, 0.5f,    // Vertex 11

      // Left face
      -0.5f, -0.5f, -0.5f,  // Vertex 12
      -0.5f, -0.5f, 0.5f,   // Vertex 13
      -0.5f, 0.5f, 0.5f,    // Vertex 14
      -0.5f, 0.5f, -0.5f,   // Vertex 15

      // Top face
      -0.5f, 0.5f, 0.5f,   // Vertex 16
      0.5f, 0.5f, 0.5f,    // Vertex 17
      0.5f, 0.5f, -0.5f,   // Vertex 18
      -0.5f, 0.5f, -0.5f,  // Vertex 19

      // Bottom face
      -0.5f, -0.5f, -0.5f,  // Vertex 20
      0.5f, -0.5f, -0.5f,   // Vertex 21
      0.5f, -0.5f, 0.5f,    // Vertex 22
      -0.5f, -0.5f, 0.5f    // Vertex 23
  };

  // UV coordinates for each face (24 UVs, 4 per face)
  static const float uvCoords[] = {
      // Front face
      0.0f, 0.0f,  // Vertex 0
      1.0f, 0.0f,  // Vertex 1
      1.0f, 1.0f,  // Vertex 2
      0.0f, 1.0f,  // Vertex 3

      // Back face
      0.0f, 0.0f,  // Vertex 4
      1.0f, 0.0f,  // Vertex 5
      1.0f, 1.0f,  // Vertex 6
      0.0f, 1.0f,  // Vertex 7

      // Right face
      0.0f, 0.0f,  // Vertex 8
      1.0f, 0.0f,  // Vertex 9
      1.0f, 1.0f,  // Vertex 10
      0.0f, 1.0f,  // Vertex 11

      // Left face
      0.0f, 0.0f,  // Vertex 12
      1.0f, 0.0f,  // Vertex 13
      1.0f, 1.0f,  // Vertex 14
      0.0f, 1.0f,  // Vertex 15

      // Top face
      0.0f, 0.0f,  // Vertex 16
      1.0f, 0.0f,  // Vertex 17
      1.0f, 1.0f,  // Vertex 18
      0.0f, 1.0f,  // Vertex 19

      // Bottom face
      0.0f, 0.0f,  // Vertex 20
      1.0f, 0.0f,  // Vertex 21
      1.0f, 1.0f,  // Vertex 22
      0.0f, 1.0f,  // Vertex 23
  };

  // Indices for 12 triangles (6 faces)
  static const uint16_t indices[] = {// Front face
                                     0, 1, 2, 0, 2, 3,

                                     // Back face (adjusted winding)
                                     4, 5, 6, 4, 6, 7,

                                     // Right face
                                     8, 9, 10, 8, 10, 11,

                                     // Left face
                                     12, 13, 14, 12, 14, 15,

                                     // Top face
                                     16, 17, 18, 16, 18, 19,

                                     // Bottom face (adjusted winding)
                                     20, 21, 22, 20, 22, 23};

  const static short4 normals[] = {
      // Front face (normals pointing along Z+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw),

      // Back face (normals pointing along Z-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f}})
                      .xyzw),

      // Right face (normals pointing along X+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{1.0f, 0.0f, 0.0f}})
                      .xyzw),

      // Left face (normals pointing along X-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{-1.0f, 0.0f, 0.0f}})
                      .xyzw),

      // Top face (normals pointing along Y+)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f},
                                                float3{0.0f, 1.0f, 0.0f}})
                      .xyzw),

      // Bottom face (normals pointing along Y-)
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw),
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 0.0f, -1.0f},
                                                float3{0.0f, -1.0f, 0.0f}})
                      .xyzw)};

  m_poVertexBuffer = VertexBuffer::Builder()
                         .vertexCount(24)  // 4 vertices per face * 6 faces
                         .bufferCount(3)
                         .attribute(VertexAttribute::POSITION, 0,
                                    VertexBuffer::AttributeType::FLOAT3)
                         .attribute(VertexAttribute::TANGENTS, 1,
                                    VertexBuffer::AttributeType::SHORT4)
                         .attribute(VertexAttribute::UV0, 2,
                                    VertexBuffer::AttributeType::FLOAT2)  // UVs
                         .normalized(VertexAttribute::TANGENTS)
                         .build(*engine_);

  m_poVertexBuffer->setBufferAt(
      *engine_, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 2, VertexBuffer::BufferDescriptor(uvCoords, sizeof(uvCoords)));

  constexpr int indexCount = 36;
  m_poIndexBuffer = IndexBuffer::Builder()
                        .indexCount(indexCount)
                        .bufferType(IndexBuffer::IndexType::USHORT)
                        .build(*engine_);

  m_poIndexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  vBuildRenderable(engine_, material_manager);
}

void Cube::DebugPrint(const char* tag) const {
  BaseShape::DebugPrint(tag);
}

}  // namespace plugin_filament_view::shapes
