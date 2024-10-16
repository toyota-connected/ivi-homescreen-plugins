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

#include "debug_lines_system.h"
#include "filament_system.h"

#include <core/scene/geometry/ray.h>
#include <core/systems/ecsystems_manager.h>
#include <core/utils/entitytransforms.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/VertexBuffer.h>
#include <plugins/common/common.h>

using ::filament::IndexBuffer;
using ::filament::RenderableManager;
using ::filament::VertexAttribute;
using ::filament::VertexBuffer;

namespace plugin_filament_view {

/////////////////////////////////////////////////////////////////////////////////////////
DebugLine::DebugLine(filament::math::float3 startingPoint,
                     filament::math::float3 endingPoint,
                     filament::Engine* engine,
                     std::shared_ptr<utils::Entity> entity,
                     float fTimeToLive)
    : m_fRemainingTime(fTimeToLive),
      m_poEntity(std::move(entity))  // Create entity
{
  vertices_.emplace_back(startingPoint);
  vertices_.emplace_back(endingPoint);  //,
  indices_.emplace_back(0);
  indices_.emplace_back(1);

  // Initialize the VertexBuffer for the quad
  m_poVertexBuffer =
      VertexBuffer::Builder()
          .vertexCount(2)  // Four vertices for the quad
          .bufferCount(1)  // Single buffer for positions
          .attribute(filament::VertexAttribute::POSITION, 0,
                     filament::VertexBuffer::AttributeType::FLOAT3)
          .build(*engine);

  // Set vertex buffer data
  m_poVertexBuffer->setBufferAt(
      *engine, 0,
      filament::VertexBuffer::BufferDescriptor(
          vertices_.data(), vertices_.size() * sizeof(float) * 3));

  // Initialize the IndexBuffer for the quad (two triangles)
  constexpr int indexCount = 2;
  m_poIndexBuffer = filament::IndexBuffer::Builder()
                        .indexCount(indexCount)
                        .bufferType(filament::IndexBuffer::IndexType::USHORT)
                        .build(*engine);

  // Set index buffer data
  m_poIndexBuffer->setBuffer(
      *engine, filament::IndexBuffer::BufferDescriptor(
                   indices_.data(), indices_.size() * sizeof(unsigned short)));

  boundingBox_.min = startingPoint;
  boundingBox_.max = endingPoint;

  // Build the Renderable with the vertex and index buffers
  filament::RenderableManager::Builder(1)
      .boundingBox({{}, boundingBox_.extent()})
      .geometry(0, filament::RenderableManager::PrimitiveType::LINES,
                m_poVertexBuffer, m_poIndexBuffer)
      .culling(false)
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine, *m_poEntity);
}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLine::vCleanup(filament::Engine* engine) {
  if (m_poVertexBuffer) {
    engine->destroy(m_poVertexBuffer);
    m_poVertexBuffer = nullptr;
  }
  if (m_poIndexBuffer) {
    engine->destroy(m_poIndexBuffer);
    m_poIndexBuffer = nullptr;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
DebugLinesSystem::DebugLinesSystem() : m_bCurrentlyDrawingDebugLines(false) {}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLinesSystem::DebugPrint() {
  spdlog::debug("{}::{}", __FILE__, __FUNCTION__);
}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLinesSystem::vCleanup() {
  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "DebugLinesSystem::vCleanup");
  const auto engine = filamentSystem->getFilamentEngine();

  for (auto it = ourLines_.begin(); it != ourLines_.end();) {
    filamentSystem->getFilamentScene()->removeEntities((*it)->m_poEntity.get(),
                                                       1);

    // do visual cleanup here
    (*it)->vCleanup(engine);

    it = ourLines_.erase(it);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLinesSystem::vUpdate(float fElapsedTime) {
  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "DebugLinesSystem::vUpdate");
  const auto engine = filamentSystem->getFilamentEngine();

  for (auto it = ourLines_.begin(); it != ourLines_.end();) {
    (*it)->m_fRemainingTime -= fElapsedTime;

    if ((*it)->m_fRemainingTime < 0) {
      filamentSystem->getFilamentScene()->removeEntities(
          (*it)->m_poEntity.get(), 1);

      // do visual cleanup here
      (*it)->vCleanup(engine);

      it = ourLines_.erase(it);
    } else {
      ++it;
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLinesSystem::vInitSystem() {
  vRegisterMessageHandler(
      ECSMessageType::DebugLine, [this](const ECSMessage& msg) {
        SPDLOG_TRACE("Adding debug line: ");
        Ray rayInfo = msg.getData<Ray>(ECSMessageType::DebugLine);

        vAddLine(rayInfo.f3GetPosition(),
                 rayInfo.f3GetDirection() * rayInfo.dGetLength(), 10);
      });
}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLinesSystem::vShutdownSystem() {
  vCleanup();
}

/////////////////////////////////////////////////////////////////////////////////////////
void DebugLinesSystem::vAddLine(::filament::math::float3 startPoint,
                                ::filament::math::float3 endPoint,
                                float secondsTimeout) {
  if (m_bCurrentlyDrawingDebugLines == false) {
    return;
  }

  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "DebugLinesSystem::vAddLine");
  const auto engine = filamentSystem->getFilamentEngine();

  utils::EntityManager& oEntitymanager = engine->getEntityManager();
  auto oEntity = std::make_shared<utils::Entity>(oEntitymanager.create());

  auto newDebugLine = std::make_unique<DebugLine>(startPoint, endPoint, engine,
                                                  oEntity, secondsTimeout);

  filamentSystem->getFilamentScene()->addEntity(*oEntity);

  ourLines_.emplace_back(std::move(newDebugLine));
}

}  // namespace plugin_filament_view
