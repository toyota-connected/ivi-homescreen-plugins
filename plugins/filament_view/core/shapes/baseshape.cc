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

#include "baseshape.h"

#include <filament/RenderableManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
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
using ::filament::math::mat4f;
using ::filament::math::packSnorm16;
using ::filament::math::short4;
using ::utils::Entity;

BaseShape::BaseShape(const std::string& flutter_assets_path,
                     const flutter::EncodableMap& params)
    : id(0),
      type_(ShapeType::Unset),
      m_f3CenterPosition(0, 0, 0),
      m_f3ExtentsSize(0, 0, 0),
      m_f3Normal(0, 0, 0), 
      m_bDoubleSided(false), 
      m_bCullingOfObjectEnabled(true),
      m_bReceiveShadows(false),
      m_bCastShadows(false) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);
  for (auto& it : params) {
    auto key = std::get<std::string>(it.first);
    if (it.second.IsNull()) {
      SPDLOG_WARN("Shape Param ITER is null {} {} {}", key.c_str(), __FILE__,
                  __FUNCTION__);
      continue;
    }

    if (key == "id" && std::holds_alternative<int>(it.second)) {
      id = std::get<int>(it.second);
    } else if (key == "shapeType" &&
               std::holds_alternative<int32_t>(it.second)) {
      int32_t typeValue = std::get<int32_t>(it.second);
      if (typeValue > static_cast<int32_t>(shapes::BaseShape::ShapeType::Unset) &&
            typeValue < static_cast<int32_t>(shapes::BaseShape::ShapeType::Max)) {
            type_ = static_cast<ShapeType>(typeValue);
        } else {
            spdlog::error("Invalid shape type value: {}", typeValue);
        }
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
    } else if (key == "doubleSided" && std::holds_alternative<bool>(it.second)) {
      m_bDoubleSided = std::get<bool>(it.second);
    } else if (key == "cullingEnabled" && std::holds_alternative<bool>(it.second)) {
      m_bCullingOfObjectEnabled = std::get<bool>(it.second);
    } else if (key == "receiveShadows" && std::holds_alternative<bool>(it.second)) {
      m_bReceiveShadows = std::get<bool>(it.second);
    } else if (key == "castShadows" && std::holds_alternative<bool>(it.second)) {
      m_bCastShadows = std::get<bool>(it.second);
    } else if (!it.second.IsNull()) {
      // Note this might be ok to not read certain params if our derived
      // classes handle it.
      spdlog::info("[BaseShape] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(),
                                                           it.second);
    }
  }
  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

BaseShape::~BaseShape()
{
  vRemoveEntityFromScene();
  vDestroyBuffers();
}

void BaseShape::vDestroyBuffers()
{
  auto filamentEngine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();

  if (m_poVertexBuffer) {
      filamentEngine->destroy(m_poVertexBuffer);
      m_poVertexBuffer = nullptr;
  }
  if (m_poIndexBuffer) {
      filamentEngine->destroy(m_poIndexBuffer);
      m_poIndexBuffer = nullptr;
  }
}

void PrintMat4(const mat4f& matrix) {
    SPDLOG_WARN("Matrix 4x4:");
    for (int i = 0; i < 4; ++i) {
        SPDLOG_WARN("[{}, {}, {}, {}]", 
                    matrix[i][0], matrix[i][1], matrix[i][2], matrix[i][3]);
    }
}

void BaseShape::vApplyScalingAndSetTransform(::filament::Engine* engine_, filament::math::float3 scale) {
    auto& transformManager = engine_->getTransformManager();
    auto instance = transformManager.getInstance(*m_poEntity);
    if (instance) {
      #if 0 // TODO This needs to add in a rotation matrix param. Backlogged for future implementation.
        // filament::math::quatf rotation = filament::math::quatf::fromAxisAngle(filament::math::float3{0, 1, 0}, filament::math::radians(90.0f));
        // auto combinedTransform = mat4f::translation(f3GetCenterPosition()) * mat4f::rotation(rotation) * mat4f::scaling(scale);
      #endif
        auto combinedTransform = mat4f::translation(f3GetCenterPosition()) * mat4f::scaling(scale);
        transformManager.setTransform(instance, combinedTransform);
    }
}

void BaseShape::vBuildRenderable(int indexCount, ::filament::Engine* engine_,
                                  MaterialManager* material_manager) {
  
  // this will also set all the default values of the material instance from the
  // material param list
  auto materialInstanceResult =
      material_manager->getMaterialInstance(m_poMaterial->get());

  RenderableManager::Builder(1)
      .boundingBox({{}, m_f3ExtentsSize})
      .material(0, materialInstanceResult.getData().value())
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, m_poVertexBuffer,
                m_poIndexBuffer, 0, indexCount)
      .culling(m_bCullingOfObjectEnabled)
      .receiveShadows(m_bReceiveShadows)
      .castShadows(m_bCastShadows)
      .build(*engine_, *m_poEntity);

  vApplyScalingAndSetTransform(engine_, m_f3ExtentsSize);

  //vDestroyBuffers();
}

void BaseShape::vRemoveEntityFromScene() {
  if (m_poEntity == nullptr) {
    SPDLOG_WARN("Attempt to remove uninitialized shape from scene {}::{}",
                __FILE__, __FUNCTION__);
    return;
  }
  CustomModelViewer::Instance("Shape")->getFilamentScene()->removeEntities(
      m_poEntity.get(), 1);
}

void BaseShape::vAddEntityToScene() {
  if (m_poEntity == nullptr) {
    SPDLOG_WARN("Attempt to add uninitialized shape to scene {}::{}", __FILE__,
                __FUNCTION__);
    return;
  }

  CustomModelViewer::Instance("Shape")->getFilamentScene()->addEntity(
      *m_poEntity);
}

// bool BaseShape::bInitAndCreateShape(::filament::Engine* engine_,
//                                     std::shared_ptr<Entity> entityObject,
//                                     MaterialManager* material_manager) {
//   m_poEntity = std::move(entityObject);
//   // Future tasking planned for all the types to be defined here, and create
//   // based off settings sent in, for now only cube is represented.
//   createDoubleSidedCube(engine_, material_manager);
//   return true;
// }

filament::math::float3 BaseShape::f3GetCenterPosition() const {
  return m_f3CenterPosition;
}

void BaseShape::Print(const char* tag) const {
    spdlog::debug("++++++++");
    spdlog::debug("{} (Shape)", tag);

    spdlog::debug("ID: {}", id);
    spdlog::debug("Type: {}", static_cast<int>(type_));

    spdlog::debug("Center Position: x={}, y={}, z={}", m_f3CenterPosition.x, m_f3CenterPosition.y, m_f3CenterPosition.z);
    spdlog::debug("Extents Size: x={}, y={}, z={}", m_f3ExtentsSize.x, m_f3ExtentsSize.y, m_f3ExtentsSize.z);
    spdlog::debug("Normal: x={}, y={}, z={}", m_f3Normal.x, m_f3Normal.y, m_f3Normal.z);

    if (m_poMaterial.has_value()) {
        m_poMaterial.value()->Print("\tMaterial");
    }

    spdlog::debug("Double Sided: {}", m_bDoubleSided);
    spdlog::debug("Culling Enabled: {}", m_bCullingOfObjectEnabled);
    spdlog::debug("Receive Shadows: {}", m_bReceiveShadows);
    spdlog::debug("Cast Shadows: {}", m_bCastShadows);

    spdlog::debug("++++++++");
}

}  // namespace shapes
}  // namespace plugin_filament_view