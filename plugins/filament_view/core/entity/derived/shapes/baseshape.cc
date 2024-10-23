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

#include <core/components/derived/collidable.h>
#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecsystems_manager.h>
#include <core/utils/deserialize.h>
#include <core/utils/entitytransforms.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <math/norm.h>
#include <math/vec3.h>
#include <plugins/common/common.h>

namespace plugin_filament_view::shapes {

using filament::Aabb;
using filament::IndexBuffer;
using filament::RenderableManager;
using filament::VertexAttribute;
using filament::VertexBuffer;
using filament::math::float3;
using filament::math::mat3f;
using filament::math::mat4f;
using filament::math::packSnorm16;
using filament::math::short4;
using utils::Entity;

////////////////////////////////////////////////////////////////////////////
BaseShape::BaseShape()
    : EntityObject("unset name tbd"),
      m_poVertexBuffer(nullptr),
      m_poIndexBuffer(nullptr),
      type_(ShapeType::Unset),
      m_f3Normal(0, 0, 0),
      m_poMaterialInstance(
          Resource<filament::MaterialInstance*>::Error("Unset")) {}

////////////////////////////////////////////////////////////////////////////
BaseShape::BaseShape(const flutter::EncodableMap& params)
    : EntityObject("unset name tbd"),
      m_poVertexBuffer(nullptr),
      m_poIndexBuffer(nullptr),
      type_(ShapeType::Unset),
      m_f3Normal(0, 0, 0),
      m_poMaterialInstance(
          Resource<filament::MaterialInstance*>::Error("Unset")) {
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
                                          float3(0, 0, 0));
  //Deserialize::DecodeParameterWithDefault(kMaterial, m_poMaterialDefinitions,
  //                                        params);
  Deserialize::DecodeParameterWithDefault(kDoubleSided, &m_bDoubleSided, params,
                                          false);

  // if we have collidable data request, we need to build that component, as its
  // optional
  if (const auto it = params.find(flutter::EncodableValue(kCollidable));
      it != params.end() && !it->second.IsNull()) {
    // They're requesting a collidable on this object. Make one.
    auto collidableComp = std::make_shared<Collidable>(params);
    vAddComponent(std::move(collidableComp));
  }

  // if we have material definitions data request, we'll build that component
  // (optional)
  if (const auto it = params.find(flutter::EncodableValue(kMaterial));
      it != params.end() && !it->second.IsNull()) {
      auto materialDefinitions = std::make_shared<MaterialDefinitions>(std::get<flutter::EncodableMap>(it->second));
      vAddComponent(std::move(materialDefinitions));
  }

  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

////////////////////////////////////////////////////////////////////////////
BaseShape::~BaseShape() {
  vRemoveEntityFromScene();
  vDestroyBuffers();
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::vDestroyBuffers() {
  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "BaseShape::vDestroyBuffers");
  const auto filamentEngine = filamentSystem->getFilamentEngine();

  if (m_poMaterialInstance.getStatus() == Status::Success &&
      m_poMaterialInstance.getData() != nullptr) {
    filamentEngine->destroy(m_poMaterialInstance.getData().value());
    m_poMaterialInstance =
        Resource<filament::MaterialInstance*>::Error("Unset");
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

////////////////////////////////////////////////////////////////////////////
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

  const std::shared_ptr<Component> componentBT =
      GetComponentByStaticTypeID(BaseTransform::StaticGetTypeID());
  const std::shared_ptr<BaseTransform> baseTransformPtr =
      std::dynamic_pointer_cast<BaseTransform>(componentBT);

  const std::shared_ptr<Component> componentCR =
      GetComponentByStaticTypeID(CommonRenderable::StaticGetTypeID());
  const std::shared_ptr<CommonRenderable> commonRenderablePtr =
      std::dynamic_pointer_cast<CommonRenderable>(componentCR);

  other.m_poBaseTransform = std::weak_ptr<BaseTransform>(baseTransformPtr);
  other.m_poCommonRenderable =
      std::weak_ptr<CommonRenderable>(commonRenderablePtr);
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::vBuildRenderable(filament::Engine* engine_) {
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
    const auto materialSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<MaterialSystem>(
            MaterialSystem::StaticGetTypeID(), "BaseShape::vBuildRenderable");

    if (materialSystem == nullptr) {
      spdlog::error("Failed to get material system.");
    } else {
      // this will also set all the default values of the material instance from
      // the material param list

        const auto materialDefinitions = GetComponentByStaticTypeID(MaterialDefinitions::StaticGetTypeID());
        if(materialDefinitions != nullptr) {
            m_poMaterialInstance =
                materialSystem->getMaterialInstance(dynamic_cast<const MaterialDefinitions*>(materialDefinitions.get()));
        }

      if (m_poMaterialInstance.getStatus() != Status::Success) {
        spdlog::error("Failed to get material instance.");
        return;
      }
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

////////////////////////////////////////////////////////////////////////////
void BaseShape::vRemoveEntityFromScene() const {
  if (m_poEntity == nullptr) {
    SPDLOG_WARN("Attempt to remove uninitialized shape from scene {}::{}",
                __FILE__, __FUNCTION__);
    return;
  }

  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(),
          "BaseShape::vRemoveEntityFromScene");

  filamentSystem->getFilamentScene()->removeEntities(m_poEntity.get(), 1);
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::vAddEntityToScene() const {
  if (m_poEntity == nullptr) {
    SPDLOG_WARN("Attempt to add uninitialized shape to scene {}::{}", __FILE__,
                __FUNCTION__);
    return;
  }

  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(),
          "BaseShape::vRemoveEntityFromScene");
  filamentSystem->getFilamentScene()->addEntity(*m_poEntity);
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::DebugPrint() const {
  vDebugPrintComponents();
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::DebugPrint(const char* tag) const {
  spdlog::debug("++++++++ (Shape) ++++++++");
  spdlog::debug("Tag {} ID {} Type {} Wireframe {}", tag, id,
                static_cast<int>(type_), m_bIsWireframe);
  spdlog::debug("Normal: x={}, y={}, z={}", m_f3Normal.x, m_f3Normal.y,
                m_f3Normal.z);

  spdlog::debug("Double Sided: {}", m_bDoubleSided);

  DebugPrint();

  spdlog::debug("-------- (Shape) --------");
}

}  // namespace plugin_filament_view::shapes
