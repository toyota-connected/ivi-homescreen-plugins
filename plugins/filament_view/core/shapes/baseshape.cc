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
#include <math/norm.h>
#include <math/vec3.h>

#include "core/include/literals.h"
#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

#include "core/utils/entitytransforms.h"

namespace plugin_filament_view::shapes {

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
    : m_poVertexBuffer(nullptr),
      m_poIndexBuffer(nullptr),
      type_(ShapeType::Unset),
      m_f3Normal(0, 0, 0),
      m_poMaterialInstance(
          Resource<::filament::MaterialInstance*>::Error("Unset")) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  Deserialize::DecodeParameterWithDefault(kId, &id, params, 0);

  m_oBaseTransform = BaseTransform(params);
  m_oCommonRenderable = CommonRenderable(params);

  Deserialize::DecodeEnumParameterWithDefault(kShapeType, &type_, params,
                                              ShapeType::Unset);
  Deserialize::DecodeParameterWithDefault(kNormal, &m_f3Normal, params,
                                          filament::math::float3(0, 0, 0));
  Deserialize::DecodeParameterWithDefault(kMaterial, m_poMaterialDefinitions,
                                          params, flutter_assets_path);
  Deserialize::DecodeParameterWithDefault(kDoubleSided, &m_bDoubleSided, params,
                                          false);

  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

BaseShape::~BaseShape() {
  vRemoveEntityFromScene();
  vDestroyBuffers();
}

void BaseShape::vDestroyBuffers() {
  const auto filamentEngine =
      CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();

  if (m_poMaterialInstance.getStatus() == Status::Success &&
      m_poMaterialInstance.getData() != nullptr) {
    filamentEngine->destroy(m_poMaterialInstance.getData().value());
    m_poMaterialInstance =
        Resource<::filament::MaterialInstance*>::Error("Unset");
  }

  if (m_poVertexBuffer) {
    filamentEngine->destroy(m_poVertexBuffer);
    m_poVertexBuffer = nullptr;
  }
  if (m_poIndexBuffer) {
    filamentEngine->destroy(m_poIndexBuffer);
    m_poIndexBuffer = nullptr;
  }
}

void BaseShape::vBuildRenderable(::filament::Engine* engine_,
                                 MaterialManager* material_manager) {
  // this will also set all the default values of the material instance from the
  // material param list
  m_poMaterialInstance =
      material_manager->getMaterialInstance(m_poMaterialDefinitions->get());

  RenderableManager::Builder(1)
      .boundingBox({{}, m_oBaseTransform.GetExtentsSize()})
      .material(0, m_poMaterialInstance.getData().value())
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                m_poVertexBuffer, m_poIndexBuffer)
      .culling(m_oCommonRenderable.IsCullingOfObjectEnabled())
      .receiveShadows(m_oCommonRenderable.IsReceiveShadowsEnabled())
      .castShadows(m_oCommonRenderable.IsCastShadowsEnabled())
      .build(*engine_, *m_poEntity);

  EntityTransforms::vApplyTransform(m_poEntity, m_oBaseTransform.GetRotation(),
                                    m_oBaseTransform.GetScale(),
                                    m_oBaseTransform.GetCenterPosition());

  // TODO , need 'its done building callback to delete internal arrays data'
  // - note the calls are async built, but doesn't seem to be a method internal
  // to filament for when the building is complete. Further R&D is needed.
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

void BaseShape::DebugPrint(const char* tag) const {
  spdlog::debug("++++++++");
  spdlog::debug("{} (Shape)", tag);

  spdlog::debug("ID: {}", id);
  spdlog::debug("Type: {}", static_cast<int>(type_));

  spdlog::debug("Normal: x={}, y={}, z={}", m_f3Normal.x, m_f3Normal.y,
                m_f3Normal.z);

  if (m_poMaterialDefinitions.has_value()) {
    m_poMaterialDefinitions.value()->DebugPrint("\tMaterial Definitions");
  }

  m_oBaseTransform.DebugPrint();
  m_oCommonRenderable.DebugPrint();

  spdlog::debug("Double Sided: {}", m_bDoubleSided);

  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view::shapes
