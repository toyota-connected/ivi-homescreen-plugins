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

#include <core/utils/deserialize.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <math/norm.h>
#include <math/vec3.h>
#include <plugins/common/common.h>
#include <vector>

namespace plugin_filament_view::shapes {

using filament::IndexBuffer;
using filament::RenderableManager;
using filament::VertexAttribute;
using filament::VertexBuffer;
using filament::math::float3;
using filament::math::mat3f;
using utils::Entity;

////////////////////////////////////////////////////////////////////////////
Sphere::Sphere() : stacks_(20), slices_(20) {}

////////////////////////////////////////////////////////////////////////////
Sphere::Sphere(const flutter::EncodableMap& params)
    : BaseShape(params), stacks_(20), slices_(20) {
  SPDLOG_TRACE("+-{} {}", __FILE__, __FUNCTION__);

  static constexpr char kStacks[] = "stacks";
  static constexpr char kSlices[] = "slices";

  constexpr int defaultStacks = 20;
  constexpr int defaultSlices = 20;

  Deserialize::DecodeParameterWithDefault(kStacks, &stacks_, params,
                                          defaultStacks);
  Deserialize::DecodeParameterWithDefault(kSlices, &slices_, params,
                                          defaultSlices);
}

////////////////////////////////////////////////////////////////////////////
bool Sphere::bInitAndCreateShape(filament::Engine* engine_,
                                 std::shared_ptr<Entity> entityObject) {
  m_poEntity = std::move(entityObject);

  m_poVertexBuffer = nullptr;
  m_poIndexBuffer = nullptr;

  if (m_bDoubleSided) {
    createDoubleSidedSphere(engine_);
  } else {
    createSingleSidedSphere(engine_);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////
void Sphere::createSingleSidedSphere(filament::Engine* engine_) {
  const int sectors = slices_;  // Longitude, or number of vertical slices
  const int stacks = stacks_;   // Latitude, or number of horizontal slices

  const float sectorStep =
      2.0f * static_cast<float>(M_PI) / static_cast<float>(sectors);
  const float stackStep = static_cast<float>(M_PI) / static_cast<float>(stacks);

  // Generate vertices, normals, and UVs for the outer surface
  for (int i = 0; i <= stacks; ++i) {
    const float stackAngle =
        static_cast<float>(M_PI) / 2.0f -
        static_cast<float>(i) * stackStep;  // from pi/2 to -pi/2
    const float xy = cosf(stackAngle);      // r * cos(u)
    float z = sinf(stackAngle);             // r * sin(u)
    float v = static_cast<float>(i) /
              static_cast<float>(stacks);  // Latitude, y-axis UV

    for (int j = 0; j <= sectors; ++j) {
      const float sectorAngle =
          static_cast<float>(j) * sectorStep;  // from 0 to 2pi
      float x = xy * cosf(sectorAngle);        // x = r * cos(u) * cos(v)
      float y = xy * sinf(sectorAngle);        // y = r * cos(u) * sin(v)
      float u = static_cast<float>(j) /
                static_cast<float>(sectors);  // Longitude, x-axis UV

      // Add vertex position
      vertices_.emplace_back(x, y, z);

      // Add normal
      float length = sqrt(x * x + y * y + z * z);
      if (length == 0)
        length = 0.01f;
      normals_.emplace_back(x / length, y / length, z / length);

      // Add UV coordinates
      uvs_.emplace_back(u, v);
    }
  }

  // Generate indices for the outer surface
  for (int i = 0; i < stacks; ++i) {
    int k1 = i * (sectors + 1);  // Beginning of current stack
    int k2 = k1 + sectors + 1;   // Beginning of next stack

    for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
      // Middle area triangles
      indices_.push_back(static_cast<uint16_t>(k1));
      indices_.push_back(static_cast<uint16_t>(k2));
      indices_.push_back(static_cast<uint16_t>(k1 + 1));

      indices_.push_back(static_cast<uint16_t>(k1 + 1));
      indices_.push_back(static_cast<uint16_t>(k2));
      indices_.push_back(static_cast<uint16_t>(k2 + 1));
    }
  }

  // Create the vertex buffer
  m_poVertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(static_cast<unsigned int>(vertices_.size()))
          .bufferCount(3)  // Position, Normals, and UVs
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::TANGENTS, 1,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::UV0, 2,
                     VertexBuffer::AttributeType::FLOAT2)
          .build(*engine_);

  // Set buffer data
  m_poVertexBuffer->setBufferAt(
      *engine_, 0,
      VertexBuffer::BufferDescriptor(vertices_.data(),
                                     vertices_.size() * sizeof(float) * 3));
  m_poVertexBuffer->setBufferAt(
      *engine_, 1,
      VertexBuffer::BufferDescriptor(normals_.data(),
                                     normals_.size() * sizeof(float3)));
  m_poVertexBuffer->setBufferAt(
      *engine_, 2,
      VertexBuffer::BufferDescriptor(uvs_.data(),
                                     uvs_.size() * sizeof(float) * 2));

  // Create the index buffer
  const auto indexCount = static_cast<unsigned int>(indices_.size());
  m_poIndexBuffer = IndexBuffer::Builder()
                        .indexCount(indexCount)
                        .bufferType(IndexBuffer::IndexType::USHORT)
                        .build(*engine_);

  m_poIndexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(
                    indices_.data(), indices_.size() * sizeof(unsigned short)));

  vBuildRenderable(engine_);
}

////////////////////////////////////////////////////////////////////////////
void Sphere::createDoubleSidedSphere(filament::Engine* /*engine_*/) {
  // createDoubleSidedSphere - Same geometry, but do stack winding opposite and
  // positive on indice creation.
  spdlog::warn("createDoubleSidedSphere not implemented.");
}

////////////////////////////////////////////////////////////////////////////
void Sphere::CloneToOther(BaseShape& other) const {
  BaseShape::CloneToOther(other);

  const auto otherShape = dynamic_cast<Sphere*>(&other);

  otherShape->slices_ = slices_;
  otherShape->stacks_ = stacks_;
}

////////////////////////////////////////////////////////////////////////////
void Sphere::DebugPrint(const char* tag) const {
  BaseShape::DebugPrint(tag);

  spdlog::debug("++++++++");
  spdlog::debug("{} (Sphere)", tag);
  spdlog::debug("{} (Stacks)", stacks_);
  spdlog::debug("{} (Slices)", slices_);
  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view::shapes
