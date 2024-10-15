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
#include <core/utils/ibl_profiler.h>

namespace plugin_filament_view {

class FilamentSystem : public ECSystem {
 public:
  FilamentSystem() = default;

  // Disallow copy and assign.
  FilamentSystem(const FilamentSystem&) = delete;
  FilamentSystem& operator=(const FilamentSystem&) = delete;

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(FilamentSystem).hash_code();
  }

  void vInitSystem() override;
  void vUpdate(float fElapsedTime) override;
  void vShutdownSystem() override;
  void DebugPrint() override;

  [[nodiscard]] ::filament::Engine* getFilamentEngine() const {
    return fengine_;
  }

  [[nodiscard]] IBLProfiler* getIBLProfiler() const {
    return iblProfiler_.get();
  }

  [[nodiscard]] ::filament::Scene* getFilamentScene() const { return fscene_; }

  [[nodiscard]] ::filament::Renderer* getFilamentRenderer() const {
    return frenderer_;
  }

 private:
  ::filament::Engine* fengine_{};
  ::filament::Renderer* frenderer_{};
  ::filament::Scene* fscene_{};

  std::unique_ptr<IBLProfiler> iblProfiler_;
};
}  // namespace plugin_filament_view