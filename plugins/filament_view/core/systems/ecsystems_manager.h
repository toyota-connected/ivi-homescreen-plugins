#pragma once

#include <vector>
#include <memory>
#include <asio/io_context_strand.hpp>

#include <future>
#include "core/systems/base/ecsystem.h"

namespace plugin_filament_view {

class ECSystemManager {
public:
    static ECSystemManager* GetInstance() {
        static ECSystemManager instance;
        return &instance;
    }

    ECSystemManager(const ECSystemManager&) = delete;
    ECSystemManager& operator=(const ECSystemManager&) = delete;

    void vAddSystem(std::shared_ptr<ECSystem> system) {
        m_vecSystems.push_back(std::move(system));
    }

    void vRemoveSystem(const std::shared_ptr<ECSystem>& system) {
        m_vecSystems.erase(std::remove(m_vecSystems.begin(), m_vecSystems.end(), system), m_vecSystems.end());
    }

    // Send a message to all registered systems
    void vRouteMessage(const ECSMessage& msg) {
        for (const auto& system : m_vecSystems) {
            system->vSendMessage(msg);
        }
    }

    // Clear all systems
    void vRemoveAllSystems() {
        m_vecSystems.clear();
    }

    std::shared_ptr<ECSystem> poGetSystem(size_t systemTypeID) {
        for (const auto& system : m_vecSystems) {
            if (system->GetTypeID() == systemTypeID) {
                return system;
            }
        }
        return nullptr;  // If no matching system found
    }

    template <typename Target>
    std::shared_ptr<Target> poGetSystemAs(size_t systemTypeID) {
        // Retrieve the system from the manager using its type ID
        auto system = poGetSystem(systemTypeID);  // Assuming poGetSystem is a member function of SystemManager
        // Perform dynamic pointer cast to the desired type
        return std::dynamic_pointer_cast<Target>(system);
    }


    void vInitSystems();
    void vUpdate(float deltaTime);
    void vShutdownSystems();

    void StartRunLoop();
    void StopRunLoop();

    [[nodiscard]] bool bIsCompletedStopping() const {return m_bSpawnedThreadFinished;}

private:
    ECSystemManager()
        : io_context_(std::make_unique<asio::io_context>()),
          work_(asio::make_work_guard(io_context_->get_executor())),
          strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
    }

    void RunLoop();
    bool m_bIsRunning = false;
    bool m_bSpawnedThreadFinished = false;
    void ExecuteOnMainThread(float elapsedTime);

    std::thread filament_api_thread_;
    pthread_t filament_api_thread_id_{};
    std::unique_ptr<asio::io_context> io_context_;
    asio::executor_work_guard<decltype(io_context_->get_executor())> work_;
    std::unique_ptr<asio::io_context::strand> strand_;

    std::vector<std::shared_ptr<ECSystem>> m_vecSystems;
};
}