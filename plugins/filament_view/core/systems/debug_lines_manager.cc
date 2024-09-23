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

#include "debug_lines_manager.h"
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>

#include <utility>
#include "core/utils/entitytransforms.h"
#include "plugins/common/common.h"
#include "viewer/custom_model_viewer.h"

using ::filament::IndexBuffer;
using ::filament::RenderableManager;
using ::filament::VertexAttribute;
using ::filament::VertexBuffer;

namespace plugin_filament_view {

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

  filament::Aabb boundingBox;
  boundingBox.min = startingPoint;
  boundingBox.max = endingPoint;

  // Build the Renderable with the vertex and index buffers
  filament::RenderableManager::Builder(1)
      .boundingBox({{}, boundingBox.extent()})
      .geometry(0, filament::RenderableManager::PrimitiveType::LINES,
                m_poVertexBuffer, m_poIndexBuffer)
      .culling(false)
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine, *m_poEntity);
}

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

DebugLinesManager::DebugLinesManager() : m_bCurrentlyDrawingDebugLines(true) {}

DebugLinesManager* DebugLinesManager::m_poInstance = nullptr;
DebugLinesManager* DebugLinesManager::Instance() {
  if (m_poInstance == nullptr) {
    m_poInstance = new DebugLinesManager();
  }

  return m_poInstance;
}

void DebugLinesManager::DebugPrint() {}

void DebugLinesManager::vCleanup() {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  auto* engine = modelViewer->getFilamentEngine();

  for (auto it = ourLines_.begin(); it != ourLines_.end();) {
    modelViewer->getFilamentScene()->removeEntities((*it)->m_poEntity.get(), 1);

    // do visual cleanup here
    (*it)->vCleanup(engine);

    it = ourLines_.erase(it);
  }
}

void DebugLinesManager::vUpdate(float fElapsedTime) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  auto* engine = modelViewer->getFilamentEngine();

  for (auto it = ourLines_.begin(); it != ourLines_.end();) {
    (*it)->m_fRemainingTime -= fElapsedTime;

    if ((*it)->m_fRemainingTime < 0) {
      modelViewer->getFilamentScene()->removeEntities((*it)->m_poEntity.get(),
                                                      1);

      // do visual cleanup here
      (*it)->vCleanup(engine);

      it = ourLines_.erase(it);
    } else {
      ++it;
    }
  }
}

// TODO once 'System' methods are implemented this will need a shutdown method
// to remove lines

void DebugLinesManager::vAddLine(::filament::math::float3 startPoint,
                                 ::filament::math::float3 endPoint,
                                 float secondsTimeout) {
  if (m_bCurrentlyDrawingDebugLines == false)
    return;

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  auto* engine = modelViewer->getFilamentEngine();

  utils::EntityManager& oEntitymanager = engine->getEntityManager();
  auto oEntity = std::make_shared<utils::Entity>(oEntitymanager.create());

  auto newDebugLine =
      new DebugLine(startPoint, endPoint, engine, oEntity, secondsTimeout);

  modelViewer->getFilamentScene()->addEntity(*oEntity);

  ourLines_.emplace_back(newDebugLine);
}

}  // namespace plugin_filament_view
