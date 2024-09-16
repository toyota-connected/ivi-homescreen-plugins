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

#pragma once

#include <filament/Engine.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>
#include <list>
#include <vector>

using ::utils::Entity;

namespace plugin_filament_view {

class DebugLine {
 public:
  DebugLine(filament::math::float3 startingPoint,
            filament::math::float3 endingPoint,
            filament::Engine* engine,
            std::shared_ptr<utils::Entity> entity,
            float fTimeToLive);
  virtual ~DebugLine();
  void vCleanup(filament::Engine* engine);

  float m_fRemainingTime;
  std::shared_ptr<utils::Entity> m_poEntity;

  filament::VertexBuffer* m_poVertexBuffer = nullptr;
  filament::IndexBuffer* m_poIndexBuffer = nullptr;

  std::vector<::filament::math::float3> vertices_;
  std::vector<unsigned short> indices_;
};

class DebugLinesManager {
 public:
  DebugLinesManager();

  void DebugPrint();

  // Disallow copy and assign.
  DebugLinesManager(const DebugLinesManager&) = delete;
  DebugLinesManager& operator=(const DebugLinesManager&) = delete;

  void vUpdate(float fElapsedTime);

  static DebugLinesManager* Instance();

  void vAddLine(::filament::math::float3 startPoint,
                ::filament::math::float3 endPoint,
                float secondsTimeout);

 private:
  static DebugLinesManager* m_poInstance;

  bool m_bCurrentlyDrawingDebugLines = false;

  std::list<DebugLine*> ourLines_;
};

}  // namespace plugin_filament_view
