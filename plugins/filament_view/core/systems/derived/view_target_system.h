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

#include <core/systems/base/ecsystem.h>
#include <filament/Engine.h>
#include "flutter_desktop_engine_state.h"

namespace plugin_filament_view {

class ViewTarget;
class CameraManager;

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

  void vSetupViewTargetFromDesktopState(int32_t top,
                                        int32_t left,
                                        FlutterDesktopEngineState* state);
  void vInitializeFilamentInternalsWithViewTargets(uint32_t width[], uint32_t height[]);
  void vKickOffFrameRenderingLoops();
  void vSetCameraManager(CameraManager* cameraManager);
  void vSetupMessageChannels(flutter::PluginRegistrar* plugin_registrar);
  void vResizeViewTarget(size_t nWhich, double width, double height);
  void vSetViewTargetOffSet(size_t nWhich, double left, double top);

 private:
  std::vector<std::unique_ptr<ViewTarget>> m_lstViewTargets;
};
}  // namespace plugin_filament_view