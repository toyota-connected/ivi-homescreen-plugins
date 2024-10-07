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

#pragma once

#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>

#include "core/scene/geometry/direction.h"
#include "core/scene/geometry/position.h"
#include "core/scene/indirect_light/indirect_light.h"
#include "core/systems/base/ecsystem.h"
#include "core/utils/ibl_profiler.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"
#include "viewer/custom_model_viewer.h"

namespace plugin_filament_view {

class CustomModelViewer;
class IndirectLight;
class DefaultIndirectLight;
class KtxIndirectLight;
class HdrIndirectLight;

class IndirectLightSystem : public ECSystem {
 public:
  IndirectLightSystem() = default;

  void setDefaultIndirectLight();

  static std::future<Resource<std::string_view>> setIndirectLightFromKtxAsset(
      std::string path,
      double intensity);

  static std::future<Resource<std::string_view>> setIndirectLightFromKtxUrl(
      std::string url,
      double intensity);

  std::future<Resource<std::string_view>> setIndirectLightFromHdrAsset(
      std::string path,
      double intensity);

  static std::future<Resource<std::string_view>> setIndirectLightFromHdrUrl(
      std::string url,
      double intensity);

  Resource<std::string_view> loadIndirectLightHdrFromFile(
      const std::string& asset_path,
      double intensity);

  std::future<Resource<std::string_view>> setIndirectLight(
      DefaultIndirectLight* indirectLight);

  // Disallow copy and assign.
  IndirectLightSystem(const IndirectLightSystem&) = delete;
  IndirectLightSystem& operator=(const IndirectLightSystem&) = delete;

  ~IndirectLightSystem() override;

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(IndirectLightSystem).hash_code();
  }

  void vInitSystem() override;
  void vUpdate(float fElapsedTime) override;
  void vShutdownSystem() override;
  void DebugPrint() override;

 private:
  std::unique_ptr<DefaultIndirectLight> indirect_light_;
};
}  // namespace plugin_filament_view