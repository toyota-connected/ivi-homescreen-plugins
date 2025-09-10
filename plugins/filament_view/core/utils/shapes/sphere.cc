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

std::unordered_map<std::string, MeshData> _sphereMeshesCache;

MeshData ShapeUtils::CreateSphere(
  ::filament::Engine* engine,
  int stacks,
  int slices
) {
  // Check cache first
  std::string cacheKey = fmt::format("s{}_{}", stacks, slices);
  auto it = _sphereMeshesCache.find(cacheKey);
  if (it != _sphereMeshesCache.end()) {
    return it->second;
  }

  /*
   * ...otherwise generate the sphere mesh
   */
  const float sliceStep = 2.0f * kPif / static_cast<float>(slices);
  const float stackStep = kPif / static_cast<float>(stacks);

  const int vertexCount = (stacks + 1) * (slices + 1);
  const int indexCount = stacks * slices * 6;

  float3 vertices[vertexCount];
  float3 normals[vertexCount];
  unsigned short indices[indexCount];
  float2 uvs[vertexCount];

  // Generate vertices, normals, and UVs for the outer surface
  for (int i = 0; i <= stacks; ++i) {
    const float stackAngle = kPif / 2.0f - static_cast<float>(i) * stackStep;  // from pi/2 to -pi/2
    const float xy = cosf(stackAngle) * 0.5f;                                  // r * cos(u)
    float z = sinf(stackAngle) * 0.5f;                                         // r * sin(u)
    float v = static_cast<float>(i) / static_cast<float>(stacks);  // Latitude, y-axis UV

    for (int j = 0; j <= slices; ++j) {
      const float sliceAngle = static_cast<float>(j) * sliceStep;   // from 0 to 2pi
      float x = xy * cosf(sliceAngle);                               // x = r * cos(u) * cos(v)
      float y = xy * sinf(sliceAngle);                               // y = r * cos(u) * sin(v)
      float u = static_cast<float>(j) / static_cast<float>(slices);  // Longitude, x-axis UV

      // Add vertex position
      vertices[i * (slices + 1) + j] = float3(x, y, z);

      // Add normal
      float length = sqrt(x * x + y * y + z * z);
      if (length == 0) length = 0.01f;
      normals[i * (slices + 1) + j] = float3(x / length, y / length, z / length);

      // Add UV coordinates
      uvs[i * (slices + 1) + j] = float2(u, v);
    }
  }

  // Generate indices for the outer surface
  for (int i = 0; i < stacks; ++i) {
    int k1 = i * (slices + 1);  // Beginning of current stack
    int k2 = k1 + slices + 1;   // Beginning of next stack

    for (int j = 0; j < slices; ++j, ++k1, ++k2) {
      // Middle area triangles
      indices[i * slices * 6 + j * 6 + 0] = static_cast<uint16_t>(k1);
      indices[i * slices * 6 + j * 6 + 1] = static_cast<uint16_t>(k2);
      indices[i * slices * 6 + j * 6 + 2] = static_cast<uint16_t>(k1 + 1);

      indices[i * slices * 6 + j * 6 + 3] = static_cast<uint16_t>(k1 + 1);
      indices[i * slices * 6 + j * 6 + 4] = static_cast<uint16_t>(k2);
      indices[i * slices * 6 + j * 6 + 5] = static_cast<uint16_t>(k2 + 1);
    }
  }

  MeshData data;

  // Create the vertex buffer
  data.vertexBuffer = VertexBuffer::Builder()
                       .vertexCount(static_cast<unsigned int>(vertexCount))
                       .bufferCount(3)  // Position, Normals, and UVs
                       .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
                       .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::FLOAT3)
                       .attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::FLOAT2)
                       .build(*engine);

  // Set buffer data
  data.vertexBuffer->setBufferAt(
    *engine, 0,
    VertexBuffer::BufferDescriptor(vertices, vertexCount * sizeof(float3))
  );
  data.vertexBuffer->setBufferAt(
    *engine, 1, VertexBuffer::BufferDescriptor(normals, vertexCount * sizeof(float3))
  );
  data.vertexBuffer->setBufferAt(
    *engine, 2, VertexBuffer::BufferDescriptor(uvs, vertexCount * sizeof(float2))
  );

  // Create the index buffer
  data.indexBuffer = IndexBuffer::Builder()
                      .indexCount(indexCount)
                      .bufferType(IndexBuffer::IndexType::USHORT)
                      .build(*engine);

  data.indexBuffer->setBuffer(
    *engine,
    IndexBuffer::BufferDescriptor(indices, indexCount * sizeof(unsigned short))
  );

  // Save to cache
  _sphereMeshesCache[cacheKey] = data;

  return data;
}


}  // namespace plugin_filament_view