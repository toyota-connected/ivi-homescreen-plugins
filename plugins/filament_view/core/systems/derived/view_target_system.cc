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

        const auto state = msg.getData<FlutterDesktopEngineState*>(
            ECSMessageType::ViewTargetCreateRequest);
        const auto top =
            msg.getData<int>(ECSMessageType::ViewTargetCreateRequestTop);
        const auto left =
            msg.getData<int>(ECSMessageType::ViewTargetCreateRequestLeft);
        const auto width =
            msg.getData<uint32_t>(ECSMessageType::ViewTargetCreateRequestWidth);
        const auto heigth = msg.getData<uint32_t>(
            ECSMessageType::ViewTargetCreateRequestHeight);

        const auto nWhich = nSetupViewTargetFromDesktopState(top, left, state);
        vInitializeFilamentInternalsWithViewTargets(nWhich, width, heigth);

        if (m_poCamera != nullptr) {
          vSetCameraFromSerializedData();
        }

        spdlog::debug("ViewTargetCreateRequest Complete");
      });

  vRegisterMessageHandler(
      ECSMessageType::SetupMessageChannels, [this](const ECSMessage& msg) {
        spdlog::debug("SetupMessageChannels");

        const auto registrar = msg.getData<flutter::PluginRegistrar*>(
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

  vRegisterMessageHandler(
      ECSMessageType::ChangeViewQualitySettings, [this](const ECSMessage& msg) {
        spdlog::debug("ChangeViewQualitySettings");

        // Not Currently Implemented -- currently will change all view targes.
        // ChangeViewQualitySettingsWhichView
        auto settings =
            msg.getData<int>(ECSMessageType::ChangeViewQualitySettings);
        for (size_t i = 0; i < m_lstViewTargets.size(); ++i) {
          vChangeViewQualitySettings(
              i, static_cast<ViewTarget::ePredefinedQualitySettings>(settings));
        }

        spdlog::debug("ChangeViewQualitySettings Complete");

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
filament::View* ViewTargetSystem::getFilamentView(const size_t nWhich) const {
  if (nWhich >= m_lstViewTargets.size())
    return nullptr;

  return m_lstViewTargets[nWhich]->getFilamentView();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vInitializeFilamentInternalsWithViewTargets(
    const size_t nWhich,
    const uint32_t width,
    const uint32_t height) const {
  m_lstViewTargets[nWhich]->InitializeFilamentInternals(width, height);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vKickOffFrameRenderingLoops() const {
  for (const auto& viewTarget : m_lstViewTargets) {
    viewTarget->setInitialized();
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vChangeViewQualitySettings(
    size_t nWhich,
    ViewTarget::ePredefinedQualitySettings settings) const {
  m_lstViewTargets[nWhich]->vChangeQualitySettings(settings);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetCameraFromSerializedData() const {
  for (const auto& viewTarget : m_lstViewTargets) {
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
    flutter::PluginRegistrar* plugin_registrar) const {
  for (const auto& viewTarget : m_lstViewTargets) {
    viewTarget->setupMessageChannels(plugin_registrar);
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vResizeViewTarget(const size_t nWhich,
                                         const double width,
                                         const double height) const {
  m_lstViewTargets[nWhich]->resize(width, height);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetViewTargetOffSet(const size_t nWhich,
                                            const double left,
                                            const double top) const {
  m_lstViewTargets[nWhich]->setOffset(left, top);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vOnTouch(const size_t nWhich,
                                const int32_t action,
                                const int32_t point_count,
                                const size_t point_data_size,
                                const double* point_data) const {
  m_lstViewTargets[nWhich]->vOnTouch(action, point_count, point_data_size,
                                     point_data);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vChangePrimaryCameraMode(
    const size_t nWhich,
    const std::string& szValue) const {
  m_lstViewTargets[nWhich]->getCameraManager()->ChangePrimaryCameraMode(
      szValue);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vResetInertiaCameraToDefaultValues(
    const size_t nWhich) const {
  m_lstViewTargets[nWhich]
      ->getCameraManager()
      ->vResetInertiaCameraToDefaultValues();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::vSetCurrentCameraOrbitAngle(const size_t nWhich,
                                                   const float fValue) const {
  const auto camera =
      m_lstViewTargets[nWhich]->getCameraManager()->poGetPrimaryCamera();
  camera->vSetCurrentCameraOrbitAngle(fValue);
}

}  // namespace plugin_filament_view