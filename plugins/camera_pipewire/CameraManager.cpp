//
// Created by tcna on 3/26/25.
//

#include "CameraManager.h"
#include <glib/main_loop.h>
#include <cstdio>
#include <iostream>

// Static instance
CameraManager& CameraManager::instance() {
  static CameraManager s_instance;
  return s_instance;
}

CameraManager::CameraManager() {
  // Constructor does nothing yet; actual init in initialize()
}

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
                              uint32_t id,
                              uint32_t permissions,
                              const char* type,
                              uint32_t version,
                              const struct spa_dict* props) {
  if (!data) {
    std::cerr << "[Error] on_global received null data\n";
    return;
  }

  if (!props)
    return;
  const char* media_class = spa_dict_lookup(props, "media.class");
  if (!media_class || std::string(media_class) != "Video/Source")
    return;

  const char* node_name = spa_dict_lookup(props, "node.description");
  std::string name = node_name ? node_name : "Unknown";

  auto* self = static_cast<CameraManager*>(data);
  self->camera_nodes_[id] = name;
  std::cout << "[+] Camera added: " << name << " (id: " << id << ")\n";
}
void CameraManager::on_global_remove(void* data, uint32_t id) {
  if (!data) {
    std::cerr << "[Error] on_global_remove received null data\n";
    return;
  }
  auto* self = static_cast<CameraManager*>(data);
  auto it = self->camera_nodes_.find(id);
  if (it != self->camera_nodes_.end()) {
    std::cout << "[-] Camera removed: " << it->second << " (id: " << id
              << ")\n";
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
  pw_thread_loop_ = pw_thread_loop_new("camera-loop", 0);
  if (!pw_thread_loop_) {
    std::fprintf(stderr, "[CameraManager] Failed to create pw_main_loop.\n");
    return false;
  }

  // 3) Start the loop in its own thread
  int ret = pw_thread_loop_start(pw_thread_loop_);
  if (ret != 0) {
    std::fprintf(stderr,
                 "[CameraManager] Failed to start pw_thread_loop (err=%d)\n",
                 ret);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    return false;
  }

  // 4) Lock the loop for context/core creation
  pw_thread_loop_lock(pw_thread_loop_);
  {
    // We get the underlying spa_loop from the thread loop
    auto* loop = pw_thread_loop_get_loop(pw_thread_loop_);
    if (!loop) {
      std::fprintf(stderr,
                   "[CameraManager] Could not get loop from threadLoop.\n");
    } else {
      // Create PipeWire context
      pw_context_ = pw_context_new(loop, nullptr, 0);
      if (!pw_context_) {
        std::fprintf(stderr, "[CameraManager] Failed to create pw_context.\n");
      } else {
        // Connect to PipeWire core
        pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
        if (!pw_core_) {
          std::fprintf(stderr,
                       "[CameraManager] Could not connect to PW core.\n");
        }
        pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
        static pw_registry_events registry_events = {
            PW_VERSION_REGISTRY_EVENTS,
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
