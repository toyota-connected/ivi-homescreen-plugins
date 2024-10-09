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

#include "view_target_system.h"
#include "core/scene/view_target.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vInitSystem() {}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vShutdownSystem() {}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::DebugPrint() {}

////////////////////////////////////////////////////////////////////////////////////
filament::View* ViewTargetSystem::getFilamentView(size_t nWhich) const {
  if (nWhich < 0 || nWhich >= m_lstViewTargets.size())
    return nullptr;

  return m_lstViewTargets[nWhich]->getFilamentView();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vInitializeFilamentInternalsWithViewTargets(uint32_t width[]
                                                                   , uint32_t height[]) {
  int i = 0;
  for (auto& viewTarget : m_lstViewTargets) {
    viewTarget->InitializeFilamentInternals(width[i], height[i]);
    i++;
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vKickOffFrameRenderingLoops() {
  for (auto& viewTarget : m_lstViewTargets) {
    viewTarget->setInitialized();
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetCameraManager(CameraManager* cameraManager) {
  for (auto& viewTarget : m_lstViewTargets) {
    viewTarget->setCameraManager(cameraManager);
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetupViewTargetFromDesktopState(
    int32_t top,
    int32_t left,
    FlutterDesktopEngineState* state) {
  m_lstViewTargets.emplace_back(std::make_unique<ViewTarget>(top, left, state));
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetupMessageChannels(
    flutter::PluginRegistrar* plugin_registrar) {
  for (auto& viewTarget : m_lstViewTargets) {
    viewTarget->setupMessageChannels(plugin_registrar);
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vResizeViewTarget(size_t nWhich, double width, double height) {
  m_lstViewTargets[nWhich]->resize(width, height);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetViewTargetOffSet(size_t nWhich, double left, double top) {
  m_lstViewTargets[nWhich]->setOffset(left, top);
}



}  // namespace plugin_filament_view