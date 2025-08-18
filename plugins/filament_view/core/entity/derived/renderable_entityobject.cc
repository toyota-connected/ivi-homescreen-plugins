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
#include "renderable_entityobject.h"
#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/derived/material_system.h>
#include <core/systems/ecs.h>
#include <core/utils/asserts.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

//////////////////////////////////////////////////////////////////////////////////////////
void RenderableEntityObject::deserializeFrom(const flutter::EncodableMap& params) {
  EntityObject::deserializeFrom(params);

  // Transform (required)
  spdlog::trace("Making Transform...");
  addComponent(Transform(params));

  // CommonRenderable (required)
  spdlog::trace("Making CommonRenderable...");
  addComponent(CommonRenderable(params));

  // Collider (optional)
  spdlog::trace("Making Collider...");
  if (const auto it = params.find(flutter::EncodableValue(kCollider));
      it != params.end() && !it->second.IsNull()) {
    addComponent(Collider(params));
  } else {
    spdlog::trace("  This entity params has no collider");
  }
}  // namespace plugin_filament_view

void RenderableEntityObject::onInitialize() {
  // Initialize the base class
  EntityObject::onInitialize();

  // Make sure it has a Transform component
  const auto transform = getComponent<Transform>();
  if (!transform) {
    addComponent(Transform());  // init with defaults
  }

  // Make sure it has a CommonRenderable component
  const auto renderable = getComponent<CommonRenderable>();
  if (!renderable) {
    addComponent(CommonRenderable());  // init with defaults
  }
}

////////////////////////////////////////////////////////////////////////////
void RenderableEntityObject::LoadMaterialDefinitionsToMaterialInstance() {
  assertInitialized();
  const auto materialSystem = ecs->getSystem<MaterialSystem>(
    "RenderableEntityObject::BuildRenderable"
  );

  // this will also set all the default values of the material instance from
  // the material param list

  const auto materialDefinitions = getComponent<MaterialDefinitions>();
  if (materialDefinitions != nullptr) {
    m_poMaterialInstance = materialSystem->getMaterialInstance(
      dynamic_cast<const MaterialDefinitions*>(materialDefinitions.get())
    );
  } else {
    spdlog::error("MaterialDefinitions is null.");
  }

  if (m_poMaterialInstance.getStatus() != Status::Success) {
    spdlog::error("Failed to get material instance.");
  }
}

AABB RenderableEntityObject::getAABB() const {
  // Get renderable component
  const auto renderable = getComponent<CommonRenderable>();
  runtime_assert(!!renderable, "Missing CommonRenderable component");
  runtime_assert(
    !!renderable->_fInstance,
    fmt::format("CommonRenderable not initialized (is {})", renderable->_fInstance.asValue())
  );

  // Get the FilamentSystem, engine and rcm
  const auto filamentSystem = ecs->getSystem<FilamentSystem>("RenderableEntityObject::getAABB");
  runtime_assert(!!filamentSystem, "FilamentSystem not initialized");
  const auto& engine = filamentSystem->getFilamentEngine();
  const auto& rcm = engine->getRenderableManager();

  auto box = rcm.getAxisAlignedBoundingBox(renderable->_fInstance);
  spdlog::trace(
    "[{}] Entity({}) has AABB.scale: x={}, y={}, z={}", __FUNCTION__, guid_, box.halfExtent.x * 2,
    box.halfExtent.y * 2, box.halfExtent.z * 2
  );

  return box;
}

}  // namespace plugin_filament_view
