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

#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/derived/transform_system.h>
#include <core/systems/ecs.h>
#include <core/utils/deserialize.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <math/norm.h>
#include <math/vec3.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

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

////////////////////////////////////////////////////////////////////////////

void BaseShape::deserializeFrom(const flutter::EncodableMap& params) {
  RenderableEntityObject::deserializeFrom(params);

  // shapeType
  Deserialize::DecodeEnumParameterWithDefault(kShapeType, &type_, params, ShapeType::Unset);

  // normal
  Deserialize::DecodeParameterWithDefault(kNormal, &m_f3Normal, params, float3(0, 0, 0));

  // doubleSided
  m_bDoubleSided = Deserialize::DecodeParameterWithDefault<bool>(kDoubleSided, params, false);

  // Material (optional)
  if (Deserialize::HasKey(params, kMaterial)) {
    auto matdefParams = std::get<flutter::EncodableMap>(
      params.find(flutter::EncodableValue(kMaterial))->second
    );
    addComponent(Material(matdefParams));
  } else {
    spdlog::debug("This entity params has no material definitions");
  }
}

void BaseShape::onInitialize() {
  RenderableEntityObject::onInitialize();

  // Make sure it has a Material component
  const auto materialDefinitions = getComponent<Material>();
  if (!materialDefinitions) {
    spdlog::warn("BaseShape({}) has no material, adding default material", guid_);
    addComponent(kDefaultMaterial);  // init with defaults
  }
}

////////////////////////////////////////////////////////////////////////////
BaseShape::~BaseShape() {
  RemoveEntityFromScene();
  DestroyBuffers();
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::DestroyBuffers() {
  const auto filamentSystem = ECSManager::GetInstance()->getSystem<FilamentSystem>(
    "BaseShape::DestroyBuffers"
  );
  const auto filamentEngine = filamentSystem->getFilamentEngine();

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
// colliders, which would make a cascading collider chain
// NOTE: We also don't copy material definitions. (Purposefully)
void BaseShape::CloneToOther(BaseShape& other) const {
  other.m_f3Normal = m_f3Normal;
  other.m_bDoubleSided = m_bDoubleSided;
  other.m_bIsWireframe = m_bIsWireframe;
  other.type_ = type_;
  other.m_bHasTexturedMaterial = m_bHasTexturedMaterial;

  // and now components.
  this->ShallowCopyComponentToOther(Component::StaticGetTypeID<Transform>(), other);
  this->ShallowCopyComponentToOther(Component::StaticGetTypeID<CommonRenderable>(), other);

  const std::shared_ptr<Transform> transformPtr = ecs->getComponent<Transform>(guid_);

  const std::shared_ptr<CommonRenderable> commonRenderablePtr = ecs->getComponent<CommonRenderable>(
    guid_
  );

  /// TODO: ecs->addComponent
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::BuildRenderable(filament::Engine* engine_) {
  spdlog::debug("[{}] Building renderable for shape '{}'({})", __FUNCTION__, name, getGuid());
  // assertInitialized();
  // material_manager can and will be null for now on wireframe creation.

  filament::math::float3 aabb;
  switch (type_) {
    case ShapeType::Cube:
    case ShapeType::Sphere:
      aabb = {0.5f, 0.5f, 0.5f};  // NOTE: faces forward by default
      break;
    case ShapeType::Plane:
      aabb = {0.5f, 0.5f, 0.005f};  // NOTE: faces sideways by default
      break;
    default:
      aabb = {0, 0, 0};
      spdlog::error("Unknown shape type: {}", static_cast<int>(type_));
      break;
  }

  spdlog::trace("[{}] Building shape '{}'({})", __FUNCTION__, name, getGuid());

  const auto transform = getComponent<Transform>();
#if SPDLOG_LEVEL == trace
// transform->debugPrint("  ");
#endif

  spdlog::trace("[{}] AABB.scale: x={}, y={}, z={}", __FUNCTION__, aabb.x, aabb.y, aabb.z);

  const auto material = getComponent<Material>();
  const auto renderable = getComponent<CommonRenderable>();

  if (m_bIsWireframe) {
    // TODO: setup a wireframe material
    RenderableManager::Builder(1)
      .boundingBox({{}, aabb})  // center, halfExtent
      .material(0, material->_instance->getData().value())
      .geometry(0, RenderableManager::PrimitiveType::LINES, m_poVertexBuffer, m_poIndexBuffer)
      .culling(renderable->IsCullingOfObjectEnabled())
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine_, _fEntity);
  } else {
    // LoadMaterialDefinitionsToMaterialInstance();

    RenderableManager::Builder(1)
      .boundingBox({{}, aabb})
      .material(0, material->_instance->getData().value())
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, m_poVertexBuffer, m_poIndexBuffer)
      .culling(renderable->IsCullingOfObjectEnabled())
      .receiveShadows(renderable->IsReceiveShadowsEnabled())
      .castShadows(renderable->IsCastShadowsEnabled())
      .build(*engine_, _fEntity);
  }

  transform->_fInstance = engine_->getTransformManager().getInstance(_fEntity);
  renderable->_fInstance = engine_->getRenderableManager().getInstance(_fEntity);

  // Get parent entity id
  const auto parentId = transform->getParentId();

  const auto transformSystem = ecs->getSystem<TransformSystem>("BaseShape::BuildRenderable");
  if (parentId != kNullGuid) {
    // Get the parent entity object
    auto parentEntity = ecs->getEntity(parentId);

    transform->setParent(parentEntity->getGuid());
  }

  /// NOTE: why is this needed? if this is not called the collider doesn't work,
  ///       even though it's visible
  transformSystem->applyTransform(guid_, true);

  // TODO , need 'its done building callback to delete internal arrays data'
  // - note the calls are async built, but doesn't seem to be a method internal
  // to filament for when the building is complete. Further R&D is needed.
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::RemoveEntityFromScene() const {
  if (!_fEntity) {
    spdlog::warn("Attempt to remove uninitialized shape from scene {}", __FUNCTION__);
    return;
  }

  const auto filamentSystem = ecs->getSystem<FilamentSystem>("BaseShape::RemoveEntityFromScene");

  filamentSystem->getFilamentScene()->remove(_fEntity);
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::AddEntityToScene() const {
  if (!_fEntity) {
    spdlog::warn("Attempt to add uninitialized shape to scene {}", __FUNCTION__);
    return;
  }

  const auto filamentSystem = ecs->getSystem<FilamentSystem>("BaseShape::RemoveEntityFromScene");
  filamentSystem->getFilamentScene()->addEntity(_fEntity);
}

////////////////////////////////////////////////////////////////////////////
void BaseShape::debugPrint() const { debugPrintComponents(); }

////////////////////////////////////////////////////////////////////////////
void BaseShape::debugPrint(const char* tag) const {
  spdlog::debug("++++++++ (Shape) ++++++++");
  spdlog::debug("Tag {} Type {} Wireframe {}", tag, static_cast<int>(type_), m_bIsWireframe);
  spdlog::debug("Normal: x={}, y={}, z={}", m_f3Normal.x, m_f3Normal.y, m_f3Normal.z);

  spdlog::debug("Double Sided: {}", m_bDoubleSided);

  debugPrint();

  spdlog::debug("-------- (Shape) --------");
}

}  // namespace plugin_filament_view
