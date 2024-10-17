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
#include <core/scene/view_target.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vInitSystem() {
  vRegisterMessageHandler(
      ECSMessageType::ViewTargetCreateRequest, [this](const ECSMessage& msg) {
        spdlog::debug("ViewTargetCreateRequest");

        auto state = msg.getData<FlutterDesktopEngineState*>(
            ECSMessageType::ViewTargetCreateRequest);
        auto top = msg.getData<int>(ECSMessageType::ViewTargetCreateRequestTop);
        auto left =
            msg.getData<int>(ECSMessageType::ViewTargetCreateRequestLeft);
        auto width =
            msg.getData<uint32_t>(ECSMessageType::ViewTargetCreateRequestWidth);
        auto heigth = msg.getData<uint32_t>(
            ECSMessageType::ViewTargetCreateRequestHeight);

        auto nWhich = nSetupViewTargetFromDesktopState(top, left, state);
        vInitializeFilamentInternalsWithViewTargets(nWhich, width, heigth);

        if (m_poCamera != nullptr) {
          vSetCameraFromSerializedData();
        }

        spdlog::debug("ViewTargetCreateRequest Complete");
      });

  vRegisterMessageHandler(
      ECSMessageType::SetupMessageChannels, [this](const ECSMessage& msg) {
        spdlog::debug("SetupMessageChannels");

        auto registrar = msg.getData<flutter::PluginRegistrar*>(
            ECSMessageType::SetupMessageChannels);
        vSetupMessageChannels(registrar);

        spdlog::debug("SetupMessageChannels Complete");
      });

  vRegisterMessageHandler(
      ECSMessageType::ViewTargetStartRenderingLoops,
      [this](const ECSMessage& /*msg*/) {
        spdlog::debug("ViewTargetStartRenderingLoops");
        vKickOffFrameRenderingLoops();
        spdlog::debug("ViewTargetStartRenderingLoops Complete");
      });

  vRegisterMessageHandler(
      ECSMessageType::SetCameraFromDeserializedLoad,
      [this](const ECSMessage& msg) {
        spdlog::debug("SetCameraFromDeserializedLoad");
        m_poCamera =
            msg.getData<Camera*>(ECSMessageType::SetCameraFromDeserializedLoad)
                ->clone();
        spdlog::debug("SetCameraFromDeserializedLoad Complete");

        vSetCameraFromSerializedData();
      });
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vShutdownSystem() {
  m_poCamera.reset();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::DebugPrint() {}

////////////////////////////////////////////////////////////////////////////////////
filament::View* ViewTargetSystem::getFilamentView(size_t nWhich) const {
  if (nWhich >= m_lstViewTargets.size())
    return nullptr;

  return m_lstViewTargets[nWhich]->getFilamentView();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vInitializeFilamentInternalsWithViewTargets(
    size_t nWhich,
    uint32_t width,
    uint32_t height) {
  m_lstViewTargets[nWhich]->InitializeFilamentInternals(width, height);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vKickOffFrameRenderingLoops() {
  for (auto& viewTarget : m_lstViewTargets) {
    viewTarget->setInitialized();
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetCameraFromSerializedData() {
  for (auto& viewTarget : m_lstViewTargets) {
    // we might get request to add new view targets as they come online
    // make sure we're not resetting older ones.
    if (viewTarget->getCameraManager()->poGetPrimaryCamera() != nullptr)
      continue;

    std::unique_ptr<Camera> clonedCamera = m_poCamera->clone();

    viewTarget->vSetupCameraManagerWithDeserializedCamera(
        std::move(clonedCamera));
  }
}

////////////////////////////////////////////////////////////////////////////////////
size_t ViewTargetSystem::nSetupViewTargetFromDesktopState(
    int32_t top,
    int32_t left,
    FlutterDesktopEngineState* state) {
  m_lstViewTargets.emplace_back(std::make_unique<ViewTarget>(top, left, state));
  return m_lstViewTargets.size() - 1;
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
                                                const std::string& szValue) {
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