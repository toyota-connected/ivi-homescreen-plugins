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

#include "CameraManager.h"
#include <spdlog/spdlog.h>
#include <iostream>

// Static instance
CameraManager& CameraManager::instance() {
  static CameraManager s_instance;
  return s_instance;
}

CameraManager::CameraManager() = default;

CameraManager::~CameraManager() {
  // Ensure shutdown is called in case user forgot
  if (initialized_) {
    shutdown();
  }
}

const std::map<uint32_t, std::string>& CameraManager::getAvailableCameras()
    const {
  return camera_nodes_;
}

// Callback function for detecting cameras
void CameraManager::on_global(void* data,
                              const uint32_t id,
                              uint32_t /*permissions*/,
                              const char* /*type*/,
                              uint32_t /*version*/,
                              const struct spa_dict* props) {
  if (!data) {
    std::cerr << "[Error] on_global received null data\n";
    return;
  }

  if (!props)
    return;

  if (const char* media_class = spa_dict_lookup(props, "media.class");
      !media_class || std::string(media_class) != "Video/Source")
    return;

  const char* node_name = spa_dict_lookup(props, "node.description");
  const std::string name = node_name ? node_name : "Unknown";

  auto* self = static_cast<CameraManager*>(data);
  self->camera_nodes_[id] = name;
  spdlog::debug("[+] camera added: {} (camera_id: {})", name, id);
}
void CameraManager::on_global_remove(void* data, const uint32_t id) {
  if (!data) {
    spdlog::error("[error] on_global_remove received null data");
    return;
  }
  auto* self = static_cast<CameraManager*>(data);
  if (auto it = self->camera_nodes_.find(id); it != self->camera_nodes_.end()) {
    spdlog::debug("[-] camera removed: {} (camera_id: {})", it->second, id);
    self->camera_nodes_.erase(it);
  }
}

bool CameraManager::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    // Already initialized
    return true;
  }

  // 1) Initialize PipeWire library (safe to call once)
  pw_init(nullptr, nullptr);

  // 2) Create main loop, context, and core
  pw_thread_loop_ = pw_thread_loop_new("camera-loop", nullptr);
  if (!pw_thread_loop_) {
    spdlog::error("[CameraManager] failed to create pw_main_loop.");
    return false;
  }

  // 3) Start the loop in its own thread
  int ret = pw_thread_loop_start(pw_thread_loop_);
  if (ret != 0) {
    spdlog::error("[CameraManager] failed to start pw_thread_loop (err={})",
                  ret);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    return false;
  }

  // 4) Lock the loop for context/core creation
  pw_thread_loop_lock(pw_thread_loop_);
  {
    // We get the underlying spa_loop from the thread loop
    if (auto* loop = pw_thread_loop_get_loop(pw_thread_loop_); !loop) {
      spdlog::error("[CameraManager] could not get loop from threadLoop.");
    } else {
      // Create PipeWire context
      pw_context_ = pw_context_new(loop, nullptr, 0);
      if (!pw_context_) {
        spdlog::error("[CameraManager] failed to create pw_context.");
      } else {
        // Connect to PipeWire core
        pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
        if (!pw_core_) {
          spdlog::error("[CameraManager] could not connect to PipeWire core.");
        }
        pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
        static pw_registry_events registry_events = {
            .version = PW_VERSION_REGISTRY_EVENTS,
            .global = on_global,
            .global_remove = on_global_remove,
        };
        pw_registry_add_listener(pw_registry_, new spa_hook{}, &registry_events,
                                 this);
      }
    }
  }
  pw_thread_loop_unlock(pw_thread_loop_);

  // Check we have context & core
  if (!pw_context_ || !pw_core_) {
    // Something failed
    pw_thread_loop_stop(pw_thread_loop_);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    pw_deinit();
    return false;
  }

  initialized_ = true;
  return true;
}

void CameraManager::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return;
  }

  // 1) Stop the background thread loop
  pw_thread_loop_stop(pw_thread_loop_);

  // 2) Lock while destroying
  pw_thread_loop_lock(pw_thread_loop_);
  {
    if (pw_core_) {
      pw_core_disconnect(pw_core_);
      pw_core_ = nullptr;
    }
    if (pw_context_) {
      pw_context_destroy(pw_context_);
      pw_context_ = nullptr;
    }
  }
  pw_thread_loop_unlock(pw_thread_loop_);

  // 3) Destroy the thread loop
  pw_thread_loop_destroy(pw_thread_loop_);
  pw_thread_loop_ = nullptr;

  // 4) De-init PipeWire
  pw_deinit();
  initialized_ = false;
}
