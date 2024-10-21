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

#include <core/scene/view_target.h>
#include <core/systems/base/ecsystem.h>
#include <filament/Engine.h>
#include <flutter_desktop_engine_state.h>

namespace plugin_filament_view {

class ViewTarget;
class Camera;

class ViewTargetSystem : public ECSystem {
 public:
  ViewTargetSystem() = default;

  // Disallow copy and assign.
  ViewTargetSystem(const ViewTargetSystem&) = delete;
  ViewTargetSystem& operator=(const ViewTargetSystem&) = delete;

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(ViewTargetSystem).hash_code();
  }

  void vInitSystem() override;
  void vUpdate(float fElapsedTime) override;
  void vShutdownSystem() override;
  void DebugPrint() override;

  [[nodiscard]] filament::View* getFilamentView(size_t nWhich) const;

  // Returns the current iter that you put into the list.
  size_t nSetupViewTargetFromDesktopState(int32_t top,
                                          int32_t left,
                                          FlutterDesktopEngineState* state);
  void vInitializeFilamentInternalsWithViewTargets(size_t nWhich,
                                                   uint32_t width,
                                                   uint32_t height) const;
  void vKickOffFrameRenderingLoops() const;
  void vSetCameraFromSerializedData() const;
  void vSetupMessageChannels(flutter::PluginRegistrar* plugin_registrar) const;
  void vResizeViewTarget(size_t nWhich, double width, double height) const;
  void vSetViewTargetOffSet(size_t nWhich, double left, double top) const;

  void vOnTouch(size_t nWhich,
                int32_t action,
                int32_t point_count,
                size_t point_data_size,
                const double* point_data) const;

  void vChangePrimaryCameraMode(size_t nWhich,
                                const std::string& szValue) const;
  void vResetInertiaCameraToDefaultValues(size_t nWhich) const;
  void vSetCurrentCameraOrbitAngle(size_t nWhich, float fValue) const;

  void vChangeViewQualitySettings(
      size_t nWhich,
      ViewTarget::ePredefinedQualitySettings settings) const;

 private:
  std::vector<std::unique_ptr<ViewTarget>> m_lstViewTargets;

  std::unique_ptr<Camera> m_poCamera;
};
}  // namespace plugin_filament_view