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

#include "shape.h"

#include <filament/RenderableManager.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>

#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

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

Shape::Shape(int32_t id,
             ::filament::math::float3 centerPosition,
             ::filament::math::float3 normal,
             Material material)
    : m_f3CenterPosition(centerPosition),
      m_f3Normal(normal),
      m_f3ExtentsSize(0, 0, 0) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);
  SPDLOG_ERROR("SHAPE Not Implemented");
  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

Shape::Shape(const std::string& flutter_assets_path,
             const flutter::EncodableMap& params)
    : m_f3CenterPosition(0, 0, 0),
      m_f3Normal(0, 0, 0),
      m_f3ExtentsSize(0, 0, 0) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);
  for (auto& it : params) {
    auto key = std::get<std::string>(it.first);
    if (it.second.IsNull()) {
      SPDLOG_WARN("Shape Param ITER is null {} {} {}", key.c_str(), __FILE__,
                  __FUNCTION__);
      continue;
    }

    spdlog::trace("Shape: {}", key);

    if (key == "id" && std::holds_alternative<int>(it.second)) {
      id = std::get<int>(it.second);
    } else if (key == "shapeType" &&
               std::holds_alternative<int32_t>(it.second)) {
      type_ = std::get<int32_t>(it.second);
    } else if (key == "size" &&
               std::holds_alternative<flutter::EncodableMap>(it.second)) {
      m_f3ExtentsSize =
          Deserialize::Format3(std::get<flutter::EncodableMap>(it.second));
    } else if (key == "centerPosition" &&
               std::holds_alternative<flutter::EncodableMap>(it.second)) {
      m_f3CenterPosition =
          Deserialize::Format3(std::get<flutter::EncodableMap>(it.second));
    } else if (key == "normal" &&
               std::holds_alternative<flutter::EncodableMap>(it.second)) {
      m_f3Normal =
          Deserialize::Format3(std::get<flutter::EncodableMap>(it.second));
    } else if (key == "material" &&
               std::holds_alternative<flutter::EncodableMap>(it.second)) {
      m_poMaterial = std::make_unique<Material>(
          flutter_assets_path, std::get<flutter::EncodableMap>(it.second));
    } else if (!it.second.IsNull()) {
      spdlog::debug("[Shape] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(),
                                                           it.second);
    }

    // params to add
    // .doublesided()
    // .culling(false)
    // .receiveShadows(false)
    // .castShadows(false)
  }
  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

// TODO Cleanup / Uninit / Destructor something here.
// Shape::~Shape()
//{
// TODO

// if (m_poVertexBuffer) {
//       modelViewer_->getFilamentEngine()->destroy(m_poVertexBuffer);
//   }
//   if (m_poIndexBuffer) {
//       engine_->destroy(m_poIndexBuffer);
//   }
//}

void Shape::vRemoveEntityFromScene() {
  if (m_poEntity == nullptr) {
    SPDLOG_WARN("Attempt to remove uninitialized shape from scene {}::{}",
                __FILE__, __FUNCTION__);
    return;
  }
  CustomModelViewer::Instance("Shape")->getFilamentScene()->removeEntities(
      m_poEntity.get(), 1);
}

void Shape::vAddEntityToScene() {
  if (m_poEntity == nullptr) {
    SPDLOG_WARN("Attempt to add uninitialized shape to scene {}::{}", __FILE__,
                __FUNCTION__);
    return;
  }

  CustomModelViewer::Instance("Shape")->getFilamentScene()->addEntity(
      *m_poEntity);
}

bool Shape::bInitAndCreateShape(::filament::Engine* engine_,
                                std::shared_ptr<Entity> entityObject,
                                MaterialManager* material_manager) {
  m_poEntity = std::move(entityObject);
  // Future tasking planned for all the types to be defined here, and create
  // based off settings sent in, for now only cube is represented.
  createDoubleSidedCube(engine_, material_manager);
  return true;
}

#if 1 // All this code is to be refactored to more of an OOP pattern around
      // shapes, keeping for now.

void Shape::createDoubleSidedCube(::filament::Engine* engine_,
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

  // There's a bug with this, where they *MIGHT* show up, but even then
  // totally incorrectly. Keeping for next iteration.

  // float unitVertices[] = {
  //     -0.5f, -0.5f, 0.5f,   // Vertex 0
  //      0.5f, -0.5f, 0.5f,   // Vertex 1
  //      0.5f,  0.5f, 0.5f,   // Vertex 2
  //     -0.5f,  0.5f, 0.5f,   // Vertex 3
  //     -0.5f, -0.5f, -0.5f,  // Vertex 4
  //      0.5f, -0.5f, -0.5f,  // Vertex 5
  //      0.5f,  0.5f, -0.5f,  // Vertex 6
  //     -0.5f,  0.5f, -0.5f   // Vertex 7
  // };

  // float vertices[24];
  // for (int i = 0; i < 8; ++i) {
  //     vertices[i * 3 + 0] = unitVertices[i * 3 + 0] * m_f3ExtentsSize.x;
  //     vertices[i * 3 + 1] = unitVertices[i * 3 + 1] * m_f3ExtentsSize.y;
  //     vertices[i * 3 + 2] = unitVertices[i * 3 + 2] * m_f3ExtentsSize.z;
  // }

  // for (int i = 0; i < 8; ++i) {
  //     std::cout << "Vertex " << i << ": ("
  //               << vertices[i * 3 + 0] << ", "
  //               << vertices[i * 3 + 1] << ", "
  //               << vertices[i * 3 + 2] << ")\n";
  // }

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

  VertexBuffer* vertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(8)
          .bufferCount(2)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::TANGENTS, 1,
                     VertexBuffer::AttributeType::SHORT4)
          .normalized(VertexAttribute::TANGENTS)
          .build(*engine_);

  vertexBuffer->setBufferAt(
      *engine_, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

  vertexBuffer->setBufferAt(
      *engine_, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));

  IndexBuffer* indexBuffer = IndexBuffer::Builder()
                                 .indexCount(36)
                                 .bufferType(IndexBuffer::IndexType::USHORT)
                                 .build(*engine_);

  indexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  // this will also set all the default values of the material instance from the
  // material param list
  auto materialInstanceResult =
      material_manager->getMaterialInstance(m_poMaterial->get());

  RenderableManager::Builder(1)
      .boundingBox({{}, m_f3ExtentsSize})
      .material(0, materialInstanceResult.getData().value())
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vertexBuffer,
                indexBuffer, 0, 36)
      .culling(m_bCullingOfObjectEnabled)
      .receiveShadows(m_bReceiveShadows)
      .castShadows(m_bCastShadows)
      .build(*engine_, *m_poEntity);
}
#else
void Shape::createCube(::filament::Engine* engine_,
                       Entity* entityObject,
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

  VertexBuffer* vertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(4)
          .bufferCount(2)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3)
          .attribute(VertexAttribute::TANGENTS, 1,
                     VertexBuffer::AttributeType::SHORT4)
          .normalized(VertexAttribute::TANGENTS)
          .build(*engine_);

  vertexBuffer->setBufferAt(
      *engine_, 0, VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

  vertexBuffer->setBufferAt(
      *engine_, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));

  // Create IndexBuffer
  IndexBuffer* indexBuffer = IndexBuffer::Builder()
                                 .indexCount(6)
                                 .bufferType(IndexBuffer::IndexType::USHORT)
                                 .build(*engine_);

  indexBuffer->setBuffer(
      *engine_, IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

  SPDLOG_DEBUG("Shape::createCube {} ", __LINE__);

  auto materialInstanceResult =
      material_manager->getMaterialInstance(material_->get());

  SPDLOG_DEBUG("Shape::createCube {} ", __LINE__);

  RenderableManager::Builder(1)
      .boundingBox({{}, {1, 1, 1}})
      .material(0, materialInstanceResult.getData().value())
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vertexBuffer,
                indexBuffer, 0, 6)
      .culling(false)
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine_, *entityObject);
}

#endif

filament::math::float3 Shape::f3GetCenterPosition() const {
  return m_f3CenterPosition;
}

void Shape::Print(const char* tag) const {
  spdlog::debug("++++++++");
  spdlog::debug("{} (Shape)", tag);
#if 0
  if (centerPosition_.has_value()) {
    centerPosition_.value()->Print("\tcenterPosition");
  }
  if (normal_.has_value()) {
    normal_.value()->Print("\tnormal");
  }
#endif
  if (m_poMaterial.has_value()) {
    m_poMaterial.value()->Print("\tsize");
  }
  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view