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

#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#pragma once

#include <pipewire/core.h>
#include <pipewire/pipewire.h>
#include <map>
#include <mutex>
#include <thread>

/**
 * @brief A singleton manager that initializes and owns the shared
 * PipeWire main loop, context, and core connection.
 *
 * Typical usage:
 *   CameraManager::instance().initialize();   // Once, at startup
 *   // Create & use your CameraStream objects...
 *   CameraManager::instance().shutdown();     // At the end, if desired
 */
class CameraManager {
 public:
  static CameraManager& instance();

  /**
   * @brief Initializes PipeWire (if not already initialized).
   *        Creates main loop, context, core, and starts background thread.
   * @return true on success, false on failure.
   */
  bool initialize();

  /**
   * @brief Shuts down the PipeWire loop/context if currently running.
   *        Joins the background thread and de-initializes PipeWire.
   */
  void shutdown();

  /**
   * @brief Returns the shared pw_thread_loop*, or nullptr if not initialized.
   */
  pw_thread_loop* threadLoop() const { return pw_thread_loop_; }

  /**
   * @brief Returns the shared pw_context*, or nullptr if not initialized.
   */
  pw_context* context() const { return pw_context_; }

  /**
   * @brief Returns the shared pw_core*, or nullptr if not initialized.
   */
  pw_core* core() const { return pw_core_; }

  const std::map<uint32_t, std::string>& getAvailableCameras() const;

  CameraManager();
  ~CameraManager();

  // Not copyable or assignable
  CameraManager(const CameraManager&) = delete;
  CameraManager& operator=(const CameraManager&) = delete;

 private:
  static void on_global(void* data,
                        uint32_t id,
                        uint32_t permissions,
                        const char* type,
                        uint32_t version,
                        const struct spa_dict* props);

  static void on_global_remove(void* data, uint32_t id);

  bool initialized_ = false;
  pw_thread_loop* pw_thread_loop_ = nullptr;
  pw_context* pw_context_ = nullptr;
  pw_core* pw_core_ = nullptr;
  pw_registry* pw_registry_ = nullptr;
  mutable std::mutex mutex_;
  std::map<uint32_t, std::string> camera_nodes_;
};

#endif  // CAMERAMANAGER_H
