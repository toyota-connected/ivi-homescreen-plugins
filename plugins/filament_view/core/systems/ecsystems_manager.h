#pragma once

#include <asio/io_context_strand.hpp>
#include <memory>
#include <vector>

#include <future>
#include <shared_mutex>

#include "core/systems/base/ecsystem.h"

namespace plugin_filament_view {

class ECSystemManager {
 public:
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

  std::shared_ptr<ECSystem> poGetSystem(size_t systemTypeID, const std::string& where);

  template <typename Target>
  std::shared_ptr<Target> poGetSystemAs(size_t systemTypeID, const std::string& where) {
    // Retrieve the system from the manager using its type ID
    auto system = poGetSystem(systemTypeID, where);
    // Perform dynamic pointer cast to the desired type
    return std::dynamic_pointer_cast<Target>(system);
  }

  void vInitSystems();
  void vUpdate(float deltaTime);
  void vShutdownSystems();

  void DebugPrint();

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
};
}  // namespace plugin_filament_view