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

#include <filament/RenderableManager.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>

#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {
namespace shapes {

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
                     const flutter::EncodableMap& params) : 
                     BaseShape(flutter_assets_path, params)
    {
  SPDLOG_TRACE("+-{} {}", __FILE__, __FUNCTION__);
}

bool Cube::bInitAndCreateShape(::filament::Engine* engine_,
                              std::shared_ptr<Entity> entityObject,
                              MaterialManager* material_manager) {
  m_poEntity = std::move(entityObject);

  if(m_bDoubleSided)
    createDoubleSidedCube(engine_, material_manager);
  else
    createSingleSidedCube(engine_, material_manager);
  return true;
}

void Cube::createDoubleSidedCube(::filament::Engine* engine_,
                                  MaterialManager* material_manager) {
  // Vertices for a cube (8 vertices)
  static const float vertices[] = {
      -0.5f, -0.5f, 0.5f,   // Vertex 0
      0.5f,  -0.5f, 0.5f,   // Vertex 1
      0.5f,  0.5f,  0.5f,   // Vertex 2
      -0.5f, 0.5f,  0.5f,   // Vertex 3
      -0.5f, -0.5f, -0.5f,  // Vertex 4
      0.5f,  -0.5f, -0.5f,  // Vertex 5
      0.5f,  0.5f,  -0.5f,  // Vertex 6
      -0.5f, 0.5f,  -0.5f   // Vertex 7
  };

  // Indices for 12 triangles (6 faces)
  static const uint16_t indices[] = {
      0, 1, 2, 0, 2, 3,  // Front face
      1, 5, 6, 1, 6, 2,  // Right face
      5, 4, 7, 5, 7, 6,  // Back face
      4, 0, 3, 4, 3, 7,  // Left face
      3, 2, 6, 3, 6, 7,  // Top face
      4, 5, 1, 4, 1, 0   // Bottom face
  };

  short4 const tbn =
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw);

  const static short4 normals[] = {tbn, tbn, tbn, tbn, tbn, tbn, tbn, tbn};

  m_poVertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(8)
          .bufferCount(2)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::TANGENTS, 1,
                     VertexBuffer::AttributeType::SHORT4)
          .normalized(VertexAttribute::TANGENTS)
          .build(*engine_);

  m_poVertexBuffer->setBufferAt(
      *engine_, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));

  constexpr int indexCount = 36;
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
  static const float vertices[] = {
      -0.5f, -0.5f, 0.5f,  // Vertex 0
      0.5f,  -0.5f, 0.5f,  // Vertex 1
      0.5f,  0.5f,  0.5f,  // Vertex 2
      -0.5f, 0.5f,  0.5f   // Vertex 3
  };

  static const uint16_t indices[] = {
      0, 1, 2,  // Triangle 1
      0, 2, 3   // Triangle 2
  };

  short4 const tbn =
      packSnorm16(mat3f::packTangentFrame(mat3f{float3{1.0f, 0.0f, 0.0f},
                                                float3{0.0f, 1.0f, 0.0f},
                                                float3{0.0f, 0.0f, 1.0f}})
                      .xyzw);

  const static short4 normals[]{tbn, tbn, tbn, tbn};

  m_poVertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(4)
          .bufferCount(2)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::TANGENTS, 1,
                     VertexBuffer::AttributeType::SHORT4)
          .normalized(VertexAttribute::TANGENTS)
          .build(*engine_);

  m_poVertexBuffer->setBufferAt(
      *engine_, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));

  // Create IndexBuffer
  constexpr int indexCount = 6;
  m_poIndexBuffer = IndexBuffer::Builder()
                                 .indexCount(indexCount)
                                 .bufferType(IndexBuffer::IndexType::USHORT)
                                 .build(*engine_);

  m_poIndexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

 
vBuildRenderable(engine_, material_manager);  
}

void Cube::Print(const char* tag) const {
    BaseShape::Print(tag);
}

}  // namespace shapes
}  // namespace plugin_filament_view