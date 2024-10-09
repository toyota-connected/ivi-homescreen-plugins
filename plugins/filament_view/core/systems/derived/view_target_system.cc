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
void ViewTargetSystem::vInitializeFilamentInternalsWithViewTargets(
    uint32_t width[],
    uint32_t height[]) {
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
void ViewTargetSystem::vSetCameraFromSerializedData(
    std::unique_ptr<Camera> camera) {
  // todo clone camera per view target (for now)
  for (auto& viewTarget : m_lstViewTargets) {
    std::unique_ptr<Camera> clonedCamera = camera->clone();

    viewTarget->vSetupCameraManagerWithDeserializedCamera(
        std::move(clonedCamera));
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
void ViewTargetSystem::vResizeViewTarget(size_t nWhich,
                                         double width,
                                         double height) {
  m_lstViewTargets[nWhich]->resize(width, height);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetViewTargetOffSet(size_t nWhich,
                                            double left,
                                            double top) {
  m_lstViewTargets[nWhich]->setOffset(left, top);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vOnTouch(size_t nWhich,
                                int32_t action,
                                int32_t point_count,
                                size_t point_data_size,
                                const double* point_data) {
  m_lstViewTargets[nWhich]->vOnTouch(action, point_count, point_data_size,
                                     point_data);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vChangePrimaryCameraMode(size_t nWhich,
                                                std::string szValue) {
  m_lstViewTargets[nWhich]->getCameraManager()->ChangePrimaryCameraMode(
      szValue);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vResetInertiaCameraToDefaultValues(size_t nWhich) {
  m_lstViewTargets[nWhich]
      ->getCameraManager()
      ->vResetInertiaCameraToDefaultValues();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetCurrentCameraOrbitAngle(size_t nWhich,
                                                   float fValue) {
  auto camera =
      m_lstViewTargets[nWhich]->getCameraManager()->poGetPrimaryCamera();
  camera->vSetCurrentCameraOrbitAngle(fValue);
}

}  // namespace plugin_filament_view