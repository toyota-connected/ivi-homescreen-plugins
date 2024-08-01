/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include "shape_manager.h"

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/View.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <math/vec3.h>
#include "asio/post.hpp"

#include "plugins/common/common.h"

namespace plugin_filament_view {

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

ShapeManager::ShapeManager(MaterialManager* material_manager)
    : material_manager_(material_manager) {
  SPDLOG_TRACE("++ShapeManager::ShapeManager");
  SPDLOG_TRACE("--ShapeManager::ShapeManager");
}

// TODO, shape vector should be owned by shapemanager instead of scene.
// backlogged.
void ShapeManager::vToggleAllShapesInScene(
    bool bValue,
    const std::vector<std::unique_ptr<Shape>>& shapes) {
  if (bValue) {
    for (int i = 0; i < shapes.size(); ++i) {
      shapes[i]->vAddEntityToScene();
    }
  } else {
    for (int i = 0; i < shapes.size(); ++i) {
      shapes[i]->vRemoveEntityFromScene();
    }
  }
}

void ShapeManager::createShapes(
    const std::vector<std::unique_ptr<Shape>>& shapes) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  filament::Engine* poFilamentEngine =
      CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
  filament::Scene* poFilamentScene =
      CustomModelViewer::Instance(__FUNCTION__)->getFilamentScene();
  utils::EntityManager& oEntitymanager = poFilamentEngine->getEntityManager();
  // oEntitymanager.create(shapes.size(), lstEntities);

  for (int i = 0; i < shapes.size(); ++i) {
    auto oEntity = std::make_shared<utils::Entity>(oEntitymanager.create());

    shapes[i]->bInitAndCreateShape(poFilamentEngine, oEntity,
                                   material_manager_);

    poFilamentScene->addEntity(*oEntity.get());

    filament::math::float3 f3GetCenterPosition =
        shapes[i]->f3GetCenterPosition();

    auto& tcm = poFilamentEngine->getTransformManager();
    tcm.setTransform(tcm.getInstance(*oEntity.get()),
                     mat4f::translation(f3GetCenterPosition));

    // To investigate a better system for implementing layer mask
    // across dart to here.
    // auto& rcm = poFilamentEngine->getRenderableManager();
    // auto instance = rcm.getInstance(*oEntity.get());
    // To investigate
    // rcm.setLayerMask(instance, 0xff, 0x00);
  }
  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

}  // namespace plugin_filament_view
