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

#include <core/components/collidable.h>
#include <filament/RenderableManager.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>

#include "core/include/literals.h"
#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

#include "core/utils/entitytransforms.h"
#include "viewer/custom_model_viewer.h"

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

BaseShape::BaseShape()
    : EntityObject("unset name tbd"),
      m_poVertexBuffer(nullptr),
      m_poIndexBuffer(nullptr),
      type_(ShapeType::Unset),
      m_f3Normal(0, 0, 0),
      m_poMaterialInstance(
          Resource<::filament::MaterialInstance*>::Error("Unset")) {}

BaseShape::BaseShape(const std::string& flutter_assets_path,
                     const flutter::EncodableMap& params)
    : EntityObject("unset name tbd"),
      m_poVertexBuffer(nullptr),
      m_poIndexBuffer(nullptr),
      type_(ShapeType::Unset),
      m_f3Normal(0, 0, 0),
      m_poMaterialInstance(
          Resource<::filament::MaterialInstance*>::Error("Unset")) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  Deserialize::DecodeParameterWithDefault(kId, &id, params, 0);

  DeserializeNameAndGlobalGuid(params);

  auto oTransform = std::make_shared<BaseTransform>(params);
  auto oCommonRenderable = std::make_shared<CommonRenderable>(params);

  m_poBaseTransform = std::weak_ptr<BaseTransform>(oTransform);
  m_poCommonRenderable = std::weak_ptr<CommonRenderable>(oCommonRenderable);

  vAddComponent(std::move(oTransform));
  vAddComponent(std::move(oCommonRenderable));

  Deserialize::DecodeEnumParameterWithDefault(kShapeType, &type_, params,
                                              ShapeType::Unset);
  Deserialize::DecodeParameterWithDefault(kNormal, &m_f3Normal, params,
                                          filament::math::float3(0, 0, 0));
  Deserialize::DecodeParameterWithDefault(kMaterial, m_poMaterialDefinitions,
                                          params, flutter_assets_path);
  Deserialize::DecodeParameterWithDefault(kDoubleSided, &m_bDoubleSided, params,
                                          false);

  // if we have collidable data request, we need to build that component, as its
  // optional
  auto it = params.find(flutter::EncodableValue(kCollidable));
  if (it != params.end() && !it->second.IsNull()) {
    // They're requesting a collidable on this object. Make one.
    auto collidableComp = std::make_shared<Collidable>(params);
    vAddComponent(std::move(collidableComp));
  }

  auto transform = m_poBaseTransform.lock();
  auto getCheck = transform.get();
  if (getCheck == nullptr) {
    SPDLOG_ERROR("GET CHECK IS NULL FIRST");
  }

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

// Unique that we don't want to copy all components, as shapes can have
// collidables, which would make a cascading collidable chain
void BaseShape::CloneToOther(BaseShape& other) const {
  other.m_f3Normal = m_f3Normal;
  other.m_bDoubleSided = m_bDoubleSided;
  other.m_bIsWireframe = m_bIsWireframe;
  other.type_ = type_;
  other.m_bHasTexturedMaterial = m_bHasTexturedMaterial;

  // and now components.
  this->vShallowCopyComponentToOther(BaseTransform::StaticGetTypeID(), other);
  this->vShallowCopyComponentToOther(CommonRenderable::StaticGetTypeID(),
                                     other);

  std::shared_ptr<Component> componentBT =
      GetComponentByStaticTypeID(BaseTransform::StaticGetTypeID());
  std::shared_ptr<BaseTransform> baseTransformPtr =
      std::dynamic_pointer_cast<BaseTransform>(componentBT);

  std::shared_ptr<Component> componentCR =
      GetComponentByStaticTypeID(CommonRenderable::StaticGetTypeID());
  std::shared_ptr<CommonRenderable> commonRenderablePtr =
      std::dynamic_pointer_cast<CommonRenderable>(componentCR);

  other.m_poBaseTransform = std::weak_ptr<BaseTransform>(baseTransformPtr);
  other.m_poCommonRenderable =
      std::weak_ptr<CommonRenderable>(commonRenderablePtr);
}

void BaseShape::vBuildRenderable(::filament::Engine* engine_,
                                 MaterialManager* material_manager) {
  // material_manager can and will be null for now on wireframe creation.

  if (m_bIsWireframe) {
    // We might want to have a specific Material for wireframes in the future.
    // m_poMaterialInstance =
    //  material_manager->getMaterialInstance(m_poMaterialDefinitions->get());
    RenderableManager::Builder(1)
        .boundingBox({{}, m_poBaseTransform.lock()->GetExtentsSize()})
        //.material(0, m_poMaterialInstance.getData().value())
        .geometry(0, RenderableManager::PrimitiveType::LINES, m_poVertexBuffer,
                  m_poIndexBuffer)
        .culling(m_poCommonRenderable.lock()->IsCullingOfObjectEnabled())
        .receiveShadows(false)
        .castShadows(false)
        .build(*engine_, *m_poEntity);
  } else {
    // this will also set all the default values of the material instance from
    // the material param list
    m_poMaterialInstance =
        material_manager->getMaterialInstance(m_poMaterialDefinitions->get());

    auto transform = m_poBaseTransform.lock();
    auto getCheck = transform.get();
    if (getCheck == nullptr) {
      SPDLOG_ERROR("GET CHECK IS NULL");
    }

    RenderableManager::Builder(1)
        .boundingBox({{}, m_poBaseTransform.lock()->GetExtentsSize()})
        .material(0, m_poMaterialInstance.getData().value())
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                  m_poVertexBuffer, m_poIndexBuffer)
        .culling(m_poCommonRenderable.lock()->IsCullingOfObjectEnabled())
        .receiveShadows(m_poCommonRenderable.lock()->IsReceiveShadowsEnabled())
        .castShadows(m_poCommonRenderable.lock()->IsCastShadowsEnabled())
        .build(*engine_, *m_poEntity);
  }

  EntityTransforms::vApplyTransform(
      m_poEntity, m_poBaseTransform.lock()->GetRotation(),
      m_poBaseTransform.lock()->GetScale(),
      m_poBaseTransform.lock()->GetCenterPosition());

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

void BaseShape::DebugPrint() const {
  vDebugPrintComponents();
}

void BaseShape::DebugPrint(const char* tag) const {
  spdlog::debug("++++++++ (Shape) ++++++++");
  spdlog::debug("Tag {} ID {} Type {} Wireframe {}", tag, id,
                static_cast<int>(type_), m_bIsWireframe);
  spdlog::debug("Normal: x={}, y={}, z={}", m_f3Normal.x, m_f3Normal.y,
                m_f3Normal.z);

  spdlog::debug("Double Sided: {}", m_bDoubleSided);

  if (m_poMaterialDefinitions.has_value()) {
    m_poMaterialDefinitions.value()->DebugPrint("\tMaterial Definitions");
  }

  DebugPrint();

  spdlog::debug("-------- (Shape) --------");
}

}  // namespace plugin_filament_view::shapes
