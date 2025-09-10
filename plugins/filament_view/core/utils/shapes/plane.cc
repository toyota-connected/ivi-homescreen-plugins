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

#include "./shape_utils.h"

namespace plugin_filament_view {

MeshData ShapeUtils::CreatePlane(::filament::Engine* engine) {
  // Vertices for a single-sided plane (4 vertices)
  static constexpr float vertices[] = {
    -0.5f, -0.5f, 0.0f,  // Vertex 0
    0.5f,  -0.5f, 0.0f,  // Vertex 1
    0.5f,  0.5f,  0.0f,  // Vertex 2
    -0.5f, 0.5f,  0.0f   // Vertex 3
  };

  // UV coordinates for the plane
  static constexpr float uvCoords[] = {
    0.0f, 0.0f,  // Vertex 0
    1.0f, 0.0f,  // Vertex 1
    1.0f, 1.0f,  // Vertex 2
    0.0f, 1.0f   // Vertex 3
  };

  // Indices for 2 triangles
  static constexpr uint16_t indices[] = {
    0, 1, 2,  // Triangle 1
    0, 2, 3   // Triangle 2
  };

  static const short4 normals[] = {
    // Normals for the front face (Z+)
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}
      ).xyzw
    )
  };

  MeshData data;

  data.vertexBuffer =
    VertexBuffer::Builder()
      .vertexCount(4)
      .bufferCount(3)  // Positions, Normals, UVs
      .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
      .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::SHORT4)
      .attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::FLOAT2)
      .normalized(VertexAttribute::TANGENTS)
      .build(*engine);

  data.vertexBuffer->setBufferAt(
    *engine, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices))
  );

  data.vertexBuffer->setBufferAt(
    *engine, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals))
  );

  data.vertexBuffer->setBufferAt(
    *engine, 2, VertexBuffer::BufferDescriptor(uvCoords, sizeof(uvCoords))
  );

  constexpr int indexCount = 6;
  data.indexBuffer = IndexBuffer::Builder()
                       .indexCount(indexCount)
                       .bufferType(IndexBuffer::IndexType::USHORT)
                       .build(*engine);

  data.indexBuffer->setBuffer(*engine, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  return data;
}

}  // namespace plugin_filament_view
