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
#include <asio/post.hpp>
#include "core/systems/ecsystems_manager.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::vInitSystem() {
  spdlog::debug("Engine creation Filament API thread: 0x{:x}", pthread_self());

  fengine_ = ::filament::Engine::create(::filament::Engine::Backend::VULKAN);
}

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::vShutdownSystem() {
  ::filament::Engine::destroy(&fengine_);
}

////////////////////////////////////////////////////////////////////////////////////
void FilamentSystem::DebugPrint() {}

}  // namespace plugin_filament_view