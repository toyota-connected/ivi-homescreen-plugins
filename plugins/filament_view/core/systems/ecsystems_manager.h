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
#include <asio/io_context_strand.hpp>
#include <future>
#include <map>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace plugin_filament_view {

class ECSystemManager {
 public:
  enum RunState {
    NotInitialized,
    Initialized,
    Running,
    ShutdownStarted,
    Shutdown
  };
  RunState getRunState() const { return m_eCurrentState; }

  static ECSystemManager* GetInstance();

  ECSystemManager(const ECSystemManager&) = delete;
  ECSystemManager& operator=(const ECSystemManager&) = delete;

  void vAddSystem(std::shared_ptr<ECSystem> system);

  void vRemoveSystem(const std::shared_ptr<ECSystem>& system) {
    std::unique_lock<std::mutex> lock(vecSystemsMutex);
    m_vecSystems.erase(
        std::remove(m_vecSystems.begin(), m_vecSystems.end(), system),
        m_vecSystems.end());
  }

  // Send a message to all registered systems
  void vRouteMessage(const ECSMessage& msg) {
    std::unique_lock<std::mutex> lock(vecSystemsMutex);
    for (const auto& system : m_vecSystems) {
      system->vSendMessage(msg);
    }
  }

  // Clear all systems
  void vRemoveAllSystems() {
    std::unique_lock<std::mutex> lock(vecSystemsMutex);
    m_vecSystems.clear();
  }

  std::shared_ptr<ECSystem> poGetSystem(size_t systemTypeID,
                                        const std::string& where);

  template <typename Target>
  std::shared_ptr<Target> poGetSystemAs(size_t systemTypeID,
                                        const std::string& where) {
    // Retrieve the system from the manager using its type ID
    auto system = poGetSystem(systemTypeID, where);
    // Perform dynamic pointer cast to the desired type
    return std::dynamic_pointer_cast<Target>(system);
  }

  void vInitSystems();
  void vUpdate(float deltaTime);
  void vShutdownSystems();

  void DebugPrint() const;

  void StartRunLoop();
  void StopRunLoop();

  [[nodiscard]] bool bIsCompletedStopping() const {
    return m_bSpawnedThreadFinished;
  }

  [[nodiscard]] pthread_t GetFilamentAPIThreadID() const {
    return filament_api_thread_id_;
  }

  [[nodiscard]] const std::thread& GetFilamentAPIThread() const {
    return filament_api_thread_;
  }

  [[nodiscard]] std::unique_ptr<asio::io_context::strand>& GetStrand() {
    return strand_;
  }

  template <typename T>
  void setConfigValue(const std::string& key, T value) {
    m_mapConfigurationValues[key] = value;
  }

  // Getter for any type of value
  template <typename T>
  T getConfigValue(const std::string& key) const {
    auto it = m_mapConfigurationValues.find(key);
    if (it != m_mapConfigurationValues.end()) {
      try {
        return std::any_cast<T>(
            it->second);  // Cast the value to the expected type
      } catch (const std::bad_any_cast& e) {
        throw std::runtime_error("Error: Incorrect type for key: " + key);
      }
    } else {
      throw std::runtime_error("Error: Key not found: " + key);
    }
  }

 private:
  ECSystemManager();
  ~ECSystemManager();

  static ECSystemManager* m_poInstance;

  void vSetupThreadingInternals();

  void RunLoop();
  std::atomic<bool> m_bIsRunning{false};
  std::atomic<bool> m_bSpawnedThreadFinished{false};
  void ExecuteOnMainThread(float elapsedTime);

  std::thread filament_api_thread_;
  pthread_t filament_api_thread_id_{};
  std::unique_ptr<asio::io_context> io_context_;
  asio::executor_work_guard<decltype(io_context_->get_executor())> work_;
  std::unique_ptr<asio::io_context::strand> strand_;
  std::thread loopThread_;

  std::atomic<bool> isHandlerExecuting{false};

  std::vector<std::shared_ptr<ECSystem>> m_vecSystems;

  std::mutex vecSystemsMutex;

  std::map<std::string, std::any> m_mapConfigurationValues;

  std::map<std::string, int> m_mapOffThreadCallers;

  RunState m_eCurrentState;
};
}  // namespace plugin_filament_view