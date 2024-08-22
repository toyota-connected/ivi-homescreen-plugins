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

#include "sphere.h"

#include <filament/RenderableManager.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>
#include <vector>

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

Sphere::Sphere(const std::string& flutter_assets_path,
               const flutter::EncodableMap& params) 
    : BaseShape(flutter_assets_path, params) {
  SPDLOG_TRACE("+-{} {}", __FILE__, __FUNCTION__);

    for (auto& it : params) {
        auto key = std::get<std::string>(it.first);
        
    if (key == "stacks" && std::holds_alternative<int>(it.second)) {
        stacks_ = std::get<int>(it.second);
        } else if (key == "slices" &&
                std::holds_alternative<int32_t>(it.second)) {
        slices_ = std::get<int>(it.second);
        }
    }
}

bool Sphere::bInitAndCreateShape(::filament::Engine* engine_,
                                 std::shared_ptr<Entity> entityObject,
                                 MaterialManager* material_manager) {
  m_poEntity = std::move(entityObject);

  if (false)
    createDoubleSidedSphere(engine_, material_manager);
  else
    createSingleSidedSphere(engine_, material_manager);

  return true;
}

void Sphere::createSingleSidedSphere(::filament::Engine* engine_,
                                     MaterialManager* material_manager) {
    const int sectors = 36;  // Longitude, or number of vertical slices
    const int stacks = 18;   // Latitude, or number of horizontal slices

    std::vector<float3> vertices;
    std::vector<float3> normals;
    std::vector<uint16_t> indices;

    float sectorStep = 2 * M_PI / sectors;
    float stackStep = M_PI / stacks;
    float sectorAngle, stackAngle;

    // Generate vertices and normals for the outer surface
    for (int i = 0; i <= stacks; ++i) {
        stackAngle = M_PI / 2.0f - (float)i * stackStep;  // from pi/2 to -pi/2
        float xy = cosf(stackAngle);                // r * cos(u)
        float z = sinf(stackAngle);                 // r * sin(u)

        for (int j = 0; j <= sectors; ++j) {
            sectorAngle = j * sectorStep;     // from 0 to 2pi

            //SPDLOG_WARN("i: {} j: {}, sectorAngle: {}", i, j, sectorAngle);

            float  x = xy * cosf(sectorAngle);             // x = r * cos(u) * cos(v)
            float y = xy * sinf(sectorAngle);             // y = r * cos(u) * sin(v)

            vertices.push_back(float3{x, y, z});
            // vertices.push_back(x);
            // vertices.push_back(y);
            // vertices.push_back(z);

            if (i == 0) {
              SPDLOG_WARN("North Pole Vertex[{}]: x: {}, y: {}, z: {}", j, x, y, z);
            }   
            //SPDLOG_WARN("i: {} j: {}, sectorAngle: {} xyz: {} {} {}", i, j, sectorAngle, x, y, z);

            float length = sqrt(x * x + y * y + z * z);
            normals.push_back(float3{x/length, y/length, z/length});
            // normals.push_back(x / length);
            // normals.push_back(y / length);
            // normals.push_back(z / length);
        }
    }

    //SPDLOG_WARN("vert count: {}", vertices.size());

    // Generate indices for the outer surface
    #if 1
    for (int i = 0; i < stacks; ++i) {
      int k1 = i * (sectors + 1);  // Beginning of current stack
      int k2 = k1 + sectors + 1;   // Beginning of next stack

      for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
          if (i == 0) {
              indices.push_back(k1);
              indices.push_back(k2);
              indices.push_back((j == sectors - 1) ? k1 - j : k1 + 1);  // Wrap to start of the ring
              SPDLOG_WARN("North Pole Triangle[{}]: k1: {}, k2: {}, k1+1: {}", j, k1, k2, k1 + 1);
          } 
          // else if (i == stacks - 1) {
          //     // South pole: All triangles connect to the last vertex
          //     indices.push_back(k1 + 1);    // Vertex in the sector ring
          //     indices.push_back(k2);        // Pole vertex
          //     indices.push_back(k2 + 1);    // Next vertex in the sector ring
          // }
          else {
              // Middle area triangles
              indices.push_back(k1);
              indices.push_back(k2);
              indices.push_back(k1 + 1);

              indices.push_back(k2);
              indices.push_back(k2 + 1);
              indices.push_back(k1 + 1);
          }
      } 
    }
    #else
    for (int lat = 0; lat < stacks; ++lat) {
        for (int lon = 0; lon < sectors; ++lon) {
            int first = lat * (sectors + 1) + lon;
            int second = first + sectors + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }
    #endif

    // for (size_t i = 0; i < indices.size(); i += 3) {
    //     SPDLOG_WARN("Triangle[{}]: {}, {}, {}", i / 3, indices[i], indices[i+1], indices[i+2]);
    // }

    // Create the vertex buffer
    m_poVertexBuffer = VertexBuffer::Builder()
                           .vertexCount(vertices.size())
                           .bufferCount(2)
                           .attribute(VertexAttribute::POSITION, 0,
                                      VertexBuffer::AttributeType::FLOAT3)
                           .attribute(VertexAttribute::TANGENTS, 1,
                                      VertexBuffer::AttributeType::FLOAT3)
                           .build(*engine_);

    // Set buffer data
    m_poVertexBuffer->setBufferAt(
        *engine_, 0,
        VertexBuffer::BufferDescriptor(vertices.data(), vertices.size() * sizeof(float3)));
    m_poVertexBuffer->setBufferAt(
         *engine_, 1,
         VertexBuffer::BufferDescriptor(normals.data(), normals.size() * sizeof(float3)));

    // Create the index buffer
    int indexCount = indices.size();
    m_poIndexBuffer = IndexBuffer::Builder()
                          .indexCount(indexCount)
                          .bufferType(IndexBuffer::IndexType::USHORT)
                          .build(*engine_);

    m_poIndexBuffer->setBuffer(
        *engine_, IndexBuffer::BufferDescriptor(indices.data(), indices.size() * sizeof(uint16_t)));

    vBuildRenderable(indexCount, engine_, material_manager);

    // RenderableManager::Builder(1)
    //     .boundingBox({{}, m_f3ExtentsSize})
    //     .material(0, materialInstanceResult.getData().value())
    //     .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, m_poVertexBuffer.get(),
    //               m_poIndexBuffer.get(), 0, indexCount)
    //     .culling(m_bCullingOfObjectEnabled)
    //     .receiveShadows(m_bReceiveShadows)
    //     .castShadows(m_bCastShadows)
    //     .build(*engine_, *m_poEntity);
}

// void Sphere::createSingleSidedSphere(::filament::Engine* engine_,
//                                      MaterialManager* material_manager) {
//   const float PI = 3.14159265359f;

//     std::vector<float> vertices;
//     std::vector<int> indices;
//     std::vector<float> normals;
//     std::vector<float> texCoords;

//     float x, y, z, xy;  // vertex position
//     float nx, ny, nz, lengthInv = 1.0f;  // normal
//     float s, t;  // texCoord

//     float sectorStep = 2 * PI / slices_;
//     float stackStep = PI / stacks_;
//     float sectorAngle, stackAngle;

//     // Generate vertices, normals, and texture coordinates
//     for (int i = 0; i <= stacks_; ++i) {
//         stackAngle = PI / 2 - i * stackStep;  // from pi/2 to -pi/2
//         xy = cosf(stackAngle);                // r * cos(u)
//         z = sinf(stackAngle);                 // r * sin(u)

//         // Add (sectorCount+1) vertices per stack
//         for (int j = 0; j <= slices_; ++j) {
//             sectorAngle = j * sectorStep;     // from 0 to 2pi

//             // Vertex position (x, y, z)
//             x = xy * cosf(sectorAngle);
//             y = xy * sinf(sectorAngle);
//             vertices.push_back(x);
//             vertices.push_back(y);
//             vertices.push_back(z);

//             // Normalized vertex normal (nx, ny, nz)
//             nx = x * lengthInv;
//             ny = y * lengthInv;
//             nz = z * lengthInv;
//             normals.push_back(nx);
//             normals.push_back(ny);
//             normals.push_back(nz);

//             // Vertex texture coordinate (s, t)
//             s = (float)j / slices_;
//             t = (float)i / stacks_;
//             texCoords.push_back(s);
//             texCoords.push_back(t);
//         }
//     }

//     // Generate CCW indices for the triangles
//     int k1, k2;
//     for (int i = 0; i < stacks_; ++i) {
//         k1 = i * (slices_ + 1);     // beginning of current stack
//         k2 = k1 + slices_ + 1;      // beginning of next stack

//         for (int j = 0; j < slices_; ++j, ++k1, ++k2) {
//             // First triangle
//             if (i != 0) {
//                 indices.push_back(k1);
//                 indices.push_back(k2);
//                 indices.push_back(k1 + 1);
//             }

//             // Second triangle
//             if (i != (stacks_ - 1)) {
//                 indices.push_back(k1 + 1);
//                 indices.push_back(k2);
//                 indices.push_back(k2 + 1);
//             }
//         }
//     }
//                            SPDLOG_ERROR("A");

//     // Create and set up the vertex buffer
//     m_poVertexBuffer = VertexBuffer::Builder()
//                            .vertexCount(vertices.size() / 3)
//                            .bufferCount(3) // For positions, normals, and UVs
//                            .attribute(VertexAttribute::POSITION, 0,
//                                       VertexBuffer::AttributeType::FLOAT3)
//                            .attribute(VertexAttribute::TANGENTS, 1,
//                                       VertexBuffer::AttributeType::FLOAT3)
//                            .attribute(VertexAttribute::UV0, 2,
//                                       VertexBuffer::AttributeType::FLOAT2)
//                            .normalized(VertexAttribute::TANGENTS)
//                            .build(*engine_);
//                            SPDLOG_ERROR("B");


//     // Set buffer data
//     m_poVertexBuffer->setBufferAt(
//         *engine_, 0,
//         VertexBuffer::BufferDescriptor(vertices.data(), vertices.size() * sizeof(float)));
//     m_poVertexBuffer->setBufferAt(
//         *engine_, 1,
//         VertexBuffer::BufferDescriptor(normals.data(), normals.size() * sizeof(float)));
//     m_poVertexBuffer->setBufferAt(
//         *engine_, 2,
//         VertexBuffer::BufferDescriptor(texCoords.data(), texCoords.size() * sizeof(float)));
//                            SPDLOG_ERROR("C");

//     // Create and set up the index buffer
//     int indexCount = indices.size();
//     m_poIndexBuffer = IndexBuffer::Builder()
//                           .indexCount(indexCount)
//                           .bufferType(IndexBuffer::IndexType::USHORT)
//                           .build(*engine_);

//                            SPDLOG_ERROR("D");
//     // Set index buffer data
//     m_poIndexBuffer->setBuffer(
//         *engine_, IndexBuffer::BufferDescriptor(indices.data(), indices.size() * sizeof(int)));

//   // auto materialInstanceResult =
//   //     material_manager->getMaterialInstance(m_poMaterial->get());

//   // RenderableManager::Builder(1)
//   //     .boundingBox({{}, m_f3ExtentsSize})
//   //     .material(0, materialInstanceResult.getData().value())
//   //     .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, m_poVertexBuffer,
//   //               m_poIndexBuffer, 0, indexCount)
//   //     .culling(m_bCullingOfObjectEnabled)
//   //     .receiveShadows(m_bReceiveShadows)
//   //     .castShadows(m_bCastShadows)
//   //     .build(*engine_, *m_poEntity);


//     // Create and set up the renderable
//     //vBuildRenderable(indexCount, engine_, material_manager);
// }


void Sphere::createDoubleSidedSphere(::filament::Engine* engine_,
                                     MaterialManager* material_manager) {
  const int sectors = slices_;  // Longitude, or number of vertical slices
  const int stacks = stacks_;   // Latitude, or number of horizontal slices

  std::vector<float> vertices;
  std::vector<uint16_t> indices;

  // Generate vertices and normals for the outer and inner surfaces
  for (int i = 0; i <= stacks; ++i) {
    float stackAngle = M_PI / 2 - i * M_PI / stacks;  // Starting from pi/2 to -pi/2
    float xy = cosf(stackAngle);  // r * cos(u)
    float z = sinf(stackAngle);   // r * sin(u)

    // Add (sectors+1) vertices per stack
    for (int j = 0; j <= sectors; ++j) {
      float sectorAngle = j * 2 * M_PI / sectors;  // 0 to 2pi

      float x = xy * cosf(sectorAngle);  // x = r * cos(u) * cos(v)
      float y = xy * sinf(sectorAngle);  // y = r * cos(u) * sin(v)
      // Outer surface
      vertices.push_back(x);
      vertices.push_back(y);
      vertices.push_back(z);
      // Inner surface (negate the normal direction)
      vertices.push_back(-x);
      vertices.push_back(-y);
      vertices.push_back(-z);
    }
  }

  // Generate indices for both outer and inner surfaces
  for (int i = 0; i < stacks; ++i) {
    int k1 = i * (sectors + 1) * 2;  // Beginning of current stack (outer)
    int k2 = k1 + (sectors + 1) * 2; // Beginning of next stack (outer)

    for (int j = 0; j < sectors; ++j, k1 += 2, k2 += 2) {
      // Outer surface triangles
      if (i != 0) {
        // Top triangle
        indices.push_back(k1);
        indices.push_back(k2);
        indices.push_back(k1 + 2);
      }

      if (i != (stacks - 1)) {
        // Bottom triangle
        indices.push_back(k1 + 2);
        indices.push_back(k2);
        indices.push_back(k2 + 2);
      }

      // Inner surface triangles (reverse winding order)
      if (i != 0) {
        // Top triangle
        indices.push_back(k1 + 1);
        indices.push_back(k1 + 3);
        indices.push_back(k2 + 1);
      }

      if (i != (stacks - 1)) {
        // Bottom triangle
        indices.push_back(k1 + 3);
        indices.push_back(k2 + 3);
        indices.push_back(k2 + 1);
      }
    }
  }

  short4 const tbn = packSnorm16(mat3f::packTangentFrame(mat3f{
      float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, float3{0.0f, 0.0f, 1.0f}}).xyzw);

  std::vector<short4> normals(vertices.size() / 3, tbn);

  m_poVertexBuffer = VertexBuffer::Builder()
                         .vertexCount(vertices.size() / 3)
                         .bufferCount(2)
                         .attribute(VertexAttribute::POSITION, 0,
                                    VertexBuffer::AttributeType::FLOAT3)
                         .attribute(VertexAttribute::TANGENTS, 1,
                                    VertexBuffer::AttributeType::SHORT4)
                         .normalized(VertexAttribute::TANGENTS)
                         .build(*engine_);

  m_poVertexBuffer->setBufferAt(
      *engine_, 0,
      VertexBuffer::BufferDescriptor(vertices.data(), vertices.size() * sizeof(float)));

  m_poVertexBuffer->setBufferAt(
      *engine_, 1,
      VertexBuffer::BufferDescriptor(normals.data(), normals.size() * sizeof(short4)));

  int indexCount = indices.size();
  m_poIndexBuffer = IndexBuffer::Builder()
                        .indexCount(indexCount)
                        .bufferType(IndexBuffer::IndexType::USHORT)
                        .build(*engine_);

  m_poIndexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices.data(), indices.size() * sizeof(uint16_t)));

  vBuildRenderable(indexCount, engine_, material_manager);
}

void Sphere::Print(const char* tag) const {
  BaseShape::Print(tag);
}

}  // namespace shapes
}  // namespace plugin_filament_view
