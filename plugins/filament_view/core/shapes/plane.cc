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

#include "plane.h"

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

Plane::Plane(const std::string& flutter_assets_path,
             const flutter::EncodableMap& params) 
    : BaseShape(flutter_assets_path, params) {
  SPDLOG_TRACE("+-{} {}", __FILE__, __FUNCTION__);
}

bool Plane::bInitAndCreateShape(::filament::Engine* engine_,
                                std::shared_ptr<Entity> entityObject,
                                MaterialManager* material_manager) {
  m_poEntity = std::move(entityObject);

  if (m_bDoubleSided)
    createDoubleSidedPlane(engine_, material_manager);
  else
    createSingleSidedPlane(engine_, material_manager);

  return true;
}

void Plane::createDoubleSidedPlane(::filament::Engine* engine_,
                                   MaterialManager* material_manager) {
  // Vertices for a plane (4 vertices for each side, 8 in total)
  static const float vertices[] = {
      -0.5f, -0.5f, 0.0f,  // Front face, Vertex 0
      0.5f,  -0.5f, 0.0f,  // Front face, Vertex 1
      0.5f,  0.5f,  0.0f,  // Front face, Vertex 2
      -0.5f, 0.5f,  0.0f,  // Front face, Vertex 3
      -0.5f, -0.5f, 0.0f,  // Back face, Vertex 4
      0.5f,  -0.5f, 0.0f,  // Back face, Vertex 5
      0.5f,  0.5f,  0.0f,  // Back face, Vertex 6
      -0.5f, 0.5f,  0.0f   // Back face, Vertex 7
  };

  // Indices for 2 triangles per side, 12 indices in total
  static const uint16_t indices[] = {
      0, 1, 2, 0, 2, 3,  // Front face
      4, 6, 5, 4, 7, 6   // Back face
  };

  short4 const tbn = packSnorm16(mat3f::packTangentFrame(mat3f{
      float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}).xyzw);

  const static short4 normals[] = {tbn, tbn, tbn, tbn, tbn, tbn, tbn, tbn};

  m_poVertexBuffer = VertexBuffer::Builder()
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

  constexpr int indexCount = 12;
  m_poIndexBuffer = IndexBuffer::Builder()
                        .indexCount(indexCount)
                        .bufferType(IndexBuffer::IndexType::USHORT)
                        .build(*engine_);

  m_poIndexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  vBuildRenderable(engine_, material_manager);
}

void Plane::createSingleSidedPlane(::filament::Engine* engine_,
                                   MaterialManager* material_manager) {
  // Vertices for a single-sided plane (4 vertices)
  static const float vertices[] = {
      -0.5f, -0.5f, 0.0f,  // Vertex 0
      0.5f,  -0.5f, 0.0f,  // Vertex 1
      0.5f,  0.5f,  0.0f,  // Vertex 2
      -0.5f, 0.5f,  0.0f   // Vertex 3
  };

  // Indices for 2 triangles
  static const uint16_t indices[] = {
      0, 1, 2,  // Triangle 1
      0, 2, 3   // Triangle 2
  };

  short4 const tbn = packSnorm16(mat3f::packTangentFrame(mat3f{
      float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}).xyzw);

  const static short4 normals[] = {tbn, tbn, tbn, tbn};

  m_poVertexBuffer = VertexBuffer::Builder()
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

void Plane::Print(const char* tag) const {
  BaseShape::Print(tag);
}

}  // namespace shapes
}  // namespace plugin_filament_view
