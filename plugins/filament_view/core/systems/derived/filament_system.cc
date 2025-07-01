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
#include "filament_system.h"

#include <core/systems/ecs.h>
#include <filament/Renderer.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::vOnInitSystem() {
  DebugPrint();

  /* Note; this is checked in for future reference, on some systems this might
  be needed. TBD
  filament::Engine::Config config;
  config.minCommandBufferSizeMB = 10; // Increase from the default (1 MB) to
  handle larger workloads config.commandBufferSizeMB =
  config.minCommandBufferSizeMB * 3; // Ensure total command buffer size
  reflects the minCommandBufferSizeMB

  // Create the Filament Engine with the custom config
  fengine_ = filament::Engine::create(
      filament::Engine::Backend::VULKAN, // Use the default backend
      nullptr,                            // Use the default platform
      nullptr,                            // No shared context
      &config                             // Pass the custom config
  );*/

  fengine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
  iblProfiler_ = std::make_unique<IBLProfiler>(fengine_);
  frenderer_ = fengine_->createRenderer();
  fscene_ = fengine_->createScene();

  auto clearOptions = frenderer_->getClearOptions();
  clearOptions.clear = true;
  frenderer_->setClearOptions(clearOptions);
}

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::vShutdownSystem() {
  fengine_->destroy(fscene_);
  fengine_->destroy(frenderer_);

  iblProfiler_.reset();
  filament::Engine::destroy(&fengine_);
}

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::DebugPrint() {
  spdlog::debug("google/filament v{}", getFilamentVersionString());
  spdlog::debug("Engine creation Filament API thread: 0x{:x}", pthread_self());
}

}  // namespace plugin_filament_view
