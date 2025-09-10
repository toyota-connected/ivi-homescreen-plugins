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

MeshData ShapeUtils::createSingleSidedCube(filament::Engine* engine) {
  // Vertices for a cube (24 vertices, 4 per face)
  static constexpr float vertices[] = {
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
  static constexpr float uvCoords[] = {
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
  static constexpr uint16_t indices[] = {// Front face
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
                                         20, 21, 22, 20, 22, 23
  };

  static const short4 normals[] = {
    // Front face (normals pointing along Z+)
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
    ),

    // Back face (normals pointing along Z-)
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}}
      ).xyzw
    ),

    // Right face (normals pointing along X+)
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),

    // Left face (normals pointing along X-)
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, -1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{-1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, -1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{-1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, -1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{-1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{0.0f, 0.0f, -1.0f}, float3{0.0f, 1.0f, 0.0f}, float3{-1.0f, 0.0f, 0.0f}}
      ).xyzw
    ),

    // Top face (normals pointing along Y+)
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 1.0f, 0.0f}}
      ).xyzw
    ),

    // Bottom face (normals pointing along Y-)
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}, float3{0.0f, -1.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}, float3{0.0f, -1.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}, float3{0.0f, -1.0f, 0.0f}}
      ).xyzw
    ),
    packSnorm16(
      mat3f::packTangentFrame(
        mat3f{float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 0.0f, -1.0f}, float3{0.0f, -1.0f, 0.0f}}
      ).xyzw
    )
  };

  MeshData data;

  data.vertexBuffer =
    VertexBuffer::Builder()
      .vertexCount(24)  // 4 vertices per face * 6 faces
      .bufferCount(3)
      .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
      .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::SHORT4)
      .attribute(
        VertexAttribute::UV0, 2,
        VertexBuffer::AttributeType::FLOAT2
      )  // UVs
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

  constexpr int indexCount = 36;
  data.indexBuffer = IndexBuffer::Builder()
                       .indexCount(indexCount)
                       .bufferType(IndexBuffer::IndexType::USHORT)
                       .build(*engine);

  data.indexBuffer->setBuffer(*engine, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  return data;
}

}  // namespace plugin_filament_view
