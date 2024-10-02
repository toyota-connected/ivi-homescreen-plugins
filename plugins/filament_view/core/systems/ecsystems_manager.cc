#include <thread>
#include <chrono>
#include <asio/post.hpp>
#include <spdlog/spdlog.h>

#include "ecsystems_manager.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::StartRunLoop() {
      if(m_bIsRunning) {
        return;
      }

    m_bIsRunning = true;
    m_bSpawnedThreadFinished = false;

    filament_api_thread_ = std::thread([&]() { io_context_->run(); });
    asio::post(*strand_, [&] {
      filament_api_thread_id_ = pthread_self();
      // spdlog::debug("Filament API thread: 0x{:x}", filament_api_thread_id_);
    });

    // Launch RunLoop in a separate thread
    std::thread loopThread(&ECSystemManager::RunLoop, this);

    // Detach the thread to allow it to run independently
    loopThread.detach();
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::RunLoop() {
    const std::chrono::milliseconds frameTime(16); // ~1/60 second

    // Initialize lastFrameTime to the current time
    auto lastFrameTime = std::chrono::steady_clock::now();

    while (m_bIsRunning) {
        auto start = std::chrono::steady_clock::now();

        // Calculate the time difference between this frame and the last frame
        std::chrono::duration<float> elapsedTime = start - lastFrameTime;

        // Use asio::post to schedule work on the main thread (API thread)
        asio::post(*strand_, [elapsedTime = elapsedTime.count(), this] {
            ExecuteOnMainThread(elapsedTime); // Pass elapsed time to the main thread
        });

        // Update the time for the next frame
        lastFrameTime = start;

        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        // Sleep for the remaining time in the frame
        if (elapsed < frameTime) {
            std::this_thread::sleep_for(frameTime - elapsed);
        }
    }

    m_bSpawnedThreadFinished = true;
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::StopRunLoop() {
    m_bIsRunning = false;
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::ExecuteOnMainThread(float elapsedTime) {
    SPDLOG_INFO("execute main thread {}", elapsedTime);

    vUpdate(elapsedTime);
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vInitSystems(){
    for (const auto& system : m_vecSystems) {
      system->vInitSystem();
    }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vUpdate(float deltaTime){
    for (const auto& system : m_vecSystems) {
        system->vUpdate(deltaTime);
    }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vShutdownSystems(){
    for (const auto& system : m_vecSystems) {
        system->vShutdownSystem();
    }
}

}