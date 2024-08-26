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

// Third party package used in testing
#define USE_ICO_SPHERE 0

#if USE_ICO_SPHERE
#include "thirdparty/icospheregenerator.hpp"
#endif

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

  m_poVertexBuffer = nullptr;
  m_poIndexBuffer = nullptr;

  if (false) {
    createDoubleSidedSphere(engine_, material_manager);
  } else {
    createSingleSidedSphere(engine_, material_manager);
  }

  return true;
}

#if USE_ICO_SPHERE // Using a different package for building the verts/indices
void Sphere::createSingleSidedSphere(::filament::Engine* engine_,
                                     MaterialManager* material_manager) {

  IcosphereGenerator::Icosphere icosphere(4);  
  icosphere.generate();

  std::vector<float> vertices = icosphere.vertices;
  std::vector<unsigned int> indices = icosphere.indices;

    // Create the vertex buffer
    m_poVertexBuffer = VertexBuffer::Builder()
                           .vertexCount(vertices.size() / 3)
                           .bufferCount(1)
                           .attribute(VertexAttribute::POSITION, 0,
                                      VertexBuffer::AttributeType::FLOAT3)
                           //.attribute(VertexAttribute::TANGENTS, 1,
                           //           VertexBuffer::AttributeType::FLOAT3)
                           .build(*engine_);

    // Set buffer data
    m_poVertexBuffer->setBufferAt(
        *engine_, 0,
        VertexBuffer::BufferDescriptor(vertices.data(), vertices.size() * sizeof(float)));
    // m_poVertexBuffer->setBufferAt(
    //      *engine_, 1,
    //      VertexBuffer::BufferDescriptor(normals.data(), normals.size() * sizeof(float3)));

    // Create the index buffer
    int indexCount = indices.size();
    m_poIndexBuffer = IndexBuffer::Builder()
                          .indexCount(indexCount)
                          .bufferType(IndexBuffer::IndexType::UINT)
                          .build(*engine_);

    m_poIndexBuffer->setBuffer(
        *engine_, IndexBuffer::BufferDescriptor(indices.data(), indices.size() * sizeof(unsigned int)));

    //vBuildRenderable(indexCount, engine_, material_manager);

    RenderableManager::Builder(1)
        .boundingBox({{0}, {1}}) 
        //.material(0, materialInstanceResult.getData().value())
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, m_poVertexBuffer,
                  m_poIndexBuffer)
        .culling(false)
        .receiveShadows(false)
        .castShadows(false)
        .build(*engine_, *m_poEntity);
}
#else
void Sphere::createSingleSidedSphere(::filament::Engine* engine_,
                                     MaterialManager* material_manager) {

    const int sectors = slices_;  // Longitude, or number of vertical slices
    const int stacks = stacks_;   // Latitude, or number of horizontal slices



    float sectorStep = 2 * M_PI / sectors;
    float stackStep = M_PI / stacks;
    float sectorAngle, stackAngle;

    int atimesthrough = 0;
    int btimesthrough = 0;
    // Generate vertices and normals for the outer surface
    for (int i = 0; i <= stacks; ++i) {
        stackAngle = M_PI / 2.0f - (float)i * stackStep;  // from pi/2 to -pi/2
        float xy = cosf(stackAngle);                // r * cos(u)
        float z = sinf(stackAngle);                 // r * sin(u)

        for (int j = 0; j <= sectors; ++j) {
            sectorAngle = j * sectorStep;     // from 0 to 2pi

            atimesthrough++;
            //SPDLOG_WARN("i: {} j: {}, sectorAngle: {}", i, j, sectorAngle);

            float  x = xy * cosf(sectorAngle);             // x = r * cos(u) * cos(v)
            float y = xy * sinf(sectorAngle);             // y = r * cos(u) * sin(v)

            vertices.push_back(float3{x, y, z});
            continue;
            // vertices.push_back(x);
            // vertices.push_back(y);
            // vertices.push_back(z);

            // if (i == 0) {
            //   SPDLOG_WARN("North Pole Vertex[{}]: x: {}, y: {}, z: {}", j, x, y, z);
            // }   
            //SPDLOG_WARN("i: {} j: {}, sectorAngle: {} xyz: {} {} {}", i, j, sectorAngle, x, y, z);

            float length = sqrt(x * x + y * y + z * z);
            if(length == 0) length = 0.01f;
            normals.push_back(float3{x/length, y/length, z/length});
            // normals.push_back(x / length);
            // normals.push_back(y / length);
            // normals.push_back(z / length);


            float3 normal = float3{x / length, y / length, z / length};

            // Pack the normal into a TBN (Tangent, Bitangent, Normal) frame
            mat3f tbnFrame{
                float3{1.0f, 0.0f, 0.0f},  // Tangent (identity)
                float3{0.0f, 1.0f, 0.0f},  // Bitangent (identity)
                normal                     // Normal
            };

            short4 packedNormal = packSnorm16(mat3f::packTangentFrame(tbnFrame).xyzw);
            packedNormals.push_back(packedNormal);
        }
    }

    // Generate indices for the outer surface
    #if 1 // Different way of generating indices's
    for (int i = 0; i < stacks; ++i) {
      int k1 = i * (sectors + 1);  // Beginning of current stack
      int k2 = k1 + sectors + 1;   // Beginning of next stack

      for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
          //if (i == 0) {
          btimesthrough++;

          // if (i == 0) {
          //     indices.push_back(static_cast<uint16_t>(k1));
          //     indices.push_back(static_cast<uint16_t>(k2));
          //     indices.push_back(static_cast<uint16_t>((j == sectors - 1) ? k1 - j : k1 + 1));  // Wrap to start of the ring
          //     //SPDLOG_WARN("North Pole Triangle[{}]: k1: {}, k2: {}, k1+1: {}", j, k1, k2, k1 + 1);
          // } 
          // else 
          {
              // Middle area triangles
              indices.push_back(static_cast<uint16_t>(k1));
              indices.push_back(static_cast<uint16_t>(k2));
              indices.push_back(static_cast<uint16_t>(k1 + 1));

              indices.push_back(static_cast<uint16_t>(k1 + 1));
              indices.push_back(static_cast<uint16_t>(k2));
              indices.push_back(static_cast<uint16_t>(k2 + 1));
          }
      } 
    }
    #else // different way of generating indices.
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
    
    #if 1 // Additional Debugging information.
    size_t vertexCount = vertices.size();
    bool indicesInRange = true;

    for (size_t i = 0; i < indices.size(); ++i) {
        unsigned short index = indices[i];
        if (index >= vertexCount) {
            SPDLOG_ERROR("Index out of range! indices[{}] = {} exceeds vertex count {}.", i, index, vertexCount);
            indicesInRange = false;
        } else {
            const float3& vertex = vertices[index];
            if(vertex.x > 10 || vertex.x < -10)
              SPDLOG_INFO("indices[{}] = {}, Vertex[{}] = (x: {}, y: {}, z: {}).", i, index, index, vertex.x, vertex.y, vertex.z);
            if(vertex.y > 10 || vertex.y < -10)
              SPDLOG_INFO("indices[{}] = {}, Vertex[{}] = (x: {}, y: {}, z: {}).", i, index, index, vertex.x, vertex.y, vertex.z);
            if(vertex.z > 10 || vertex.z < -10)
              SPDLOG_INFO("indices[{}] = {}, Vertex[{}] = (x: {}, y: {}, z: {}).", i, index, index, vertex.x, vertex.y, vertex.z);
        }
    }

    if (indicesInRange) {
        SPDLOG_INFO("All indices are within the valid range of 0 to {}.", vertexCount - 1);
    } else {
        SPDLOG_WARN("Some indices were out of range. Please check the warnings above.");
    }

    SPDLOG_WARN("A and be times through {} {}", atimesthrough, btimesthrough);
    SPDLOG_WARN("Setting Vertex Buffer: count={}, size={}", vertices.size(), vertices.size() * sizeof(float3));
    //SPDLOG_WARN("Setting Normal Buffer: count={}, size={}", normals.size(), normals.size() * sizeof(float3));
    SPDLOG_WARN("Setting Index Buffer: count={}, size={}", indices.size(), indices.size() * sizeof(unsigned short));

    // for (size_t i = 0; i < indices.size(); i += 3) {
    //     SPDLOG_WARN("Triangle[{}]: {}, {}, {}", i / 3, indices[i], indices[i+1], indices[i+2]);
    // }
    #endif

    // Create the vertex buffer
    m_poVertexBuffer = VertexBuffer::Builder()
                           .vertexCount(vertices.size())
                           .bufferCount(1)
                           .attribute(VertexAttribute::POSITION, 0,
                                      VertexBuffer::AttributeType::FLOAT3)
                           //.attribute(VertexAttribute::TANGENTS, 1,
                           //           VertexBuffer::AttributeType::FLOAT3)
                           //.normalized(VertexAttribute::TANGENTS)
                           .build(*engine_);

    // Set buffer data
    m_poVertexBuffer->setBufferAt(
        *engine_, 0,
        VertexBuffer::BufferDescriptor(vertices.data(), vertices.size() * sizeof(float) * 3), 0);
    // m_poVertexBuffer->setBufferAt(
    //     *engine_, 1,
    //     VertexBuffer::BufferDescriptor(packedNormals.data(), packedNormals.size() * sizeof(short4)));
    // m_poVertexBuffer->setBufferAt(
    //      *engine_, 1,
    //      VertexBuffer::BufferDescriptor(normals.data(), normals.size() * sizeof(float3)));

    // Create the index buffer
#if 1
    int indexCount = indices.size();
    m_poIndexBuffer = IndexBuffer::Builder()
                          .indexCount(indexCount)
                          .bufferType(IndexBuffer::IndexType::USHORT)
                          .build(*engine_);

    m_poIndexBuffer->setBuffer(
        *engine_, IndexBuffer::BufferDescriptor(indices.data(), indices.size() * sizeof(unsigned short)));
#else

 // for points
    std::vector<uint16_t> kIndices(vertices.size());
for (int i = 0; i < vertices.size(); ++i) {
    kIndices[i] = static_cast<uint16_t>(i);
}

m_poIndexBuffer = IndexBuffer::Builder()
                    .indexCount(static_cast<uint32_t>(vertices.size()))
                    .bufferType(IndexBuffer::IndexType::USHORT)
                    .build(*engine_);

m_poIndexBuffer->setBuffer(
    *engine_,
    IndexBuffer::BufferDescriptor(kIndices.data(), kIndices.size() * sizeof(uint16_t), nullptr)
);
#endif

    vBuildRenderable(0, engine_, material_manager);
}
#endif




void Sphere::createDoubleSidedSphere(::filament::Engine* engine_,
                                     MaterialManager* material_manager) {
}

void Sphere::Print(const char* tag) const {
  BaseShape::Print(tag);
}

}  // namespace shapes
}  // namespace plugin_filament_view
