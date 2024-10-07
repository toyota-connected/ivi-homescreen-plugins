#include <spdlog/spdlog.h>
#include <asio/post.hpp>
#include <chrono>
#include <thread>

#include "ecsystems_manager.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
ECSystemManager* ECSystemManager::m_poInstance = nullptr;
ECSystemManager* ECSystemManager::GetInstance() {
  if (m_poInstance == nullptr) {
    spdlog::debug("Singleotn init 1 Initialize Filament API thread: 0x{:x}",
                  pthread_self());

    m_poInstance = new ECSystemManager();
  }

  return m_poInstance;
}

ECSystemManager::~ECSystemManager() {
  spdlog::debug("ECSystemManager~");
}

////////////////////////////////////////////////////////////////////////////
ECSystemManager::ECSystemManager()
    : io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(asio::make_work_guard(io_context_->get_executor())),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
  vSetupThreadingInternals();
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::StartRunLoop() {
  if (m_bIsRunning) {
    return;
  }

  m_bIsRunning = true;
  m_bSpawnedThreadFinished = false;

  // Launch RunLoop in a separate thread
  loopThread_ = std::thread(&ECSystemManager::RunLoop, this);
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vSetupThreadingInternals() {
  filament_api_thread_ = std::thread([&]() { io_context_->run(); });
  asio::post(*strand_, [&] {
    filament_api_thread_id_ = pthread_self();

    pthread_setname_np(pthread_self(), "ECSystemManagerThreadRunner");

    spdlog::debug("ECSystemManager Filament API thread: 0x{:x}",
                  filament_api_thread_id_);
  });
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::RunLoop() {
  const std::chrono::milliseconds frameTime(16);  // ~1/60 second

  // Initialize lastFrameTime to the current time
  auto lastFrameTime = std::chrono::steady_clock::now();

  while (m_bIsRunning) {
    auto start = std::chrono::steady_clock::now();

    // Calculate the time difference between this frame and the last frame
    std::chrono::duration<float> elapsedTime = start - lastFrameTime;

    if (!isHandlerExecuting.load()) {
      // Use asio::post to schedule work on the main thread (API thread)
      asio::post(*strand_, [elapsedTime = elapsedTime.count(), this] {
        isHandlerExecuting.store(true);
        try {
          ExecuteOnMainThread(
              elapsedTime);  // Pass elapsed time to the main thread
        } catch (...) {
          isHandlerExecuting.store(false);
          throw;  // Rethrow the exception after resetting the flag
        }
        isHandlerExecuting.store(false);
      });
    }

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
  if (loopThread_.joinable()) {
    loopThread_.join();
  }

  // Stop the io_context
  io_context_->stop();

  // Reset the work guard
  work_.reset();

  // Join the filament_api_thread_
  if (filament_api_thread_.joinable()) {
    filament_api_thread_.join();
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::ExecuteOnMainThread(float elapsedTime) {
  vUpdate(elapsedTime);
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vInitSystems() {
  asio::post(*ECSystemManager::GetInstance()->GetStrand(), [&] {
    for (const auto& system : m_vecSystems) {
      system->vInitSystem();
    }
  });
}

////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ECSystem> ECSystemManager::poGetSystem(size_t systemTypeID, const std::string& where) {
  auto callingThread = pthread_self();
  if (callingThread != filament_api_thread_id_) {
    spdlog::error( "From {}"
        "You're calling to get a system from an off thread, undefined "
        "experience!"
        " Use a message to do your work or grab the ecsystemmanager strand and "
        "do your work.", where);
  }

  std::unique_lock<std::mutex> lock(vecSystemsMutex);
  for (const auto& system : m_vecSystems) {
    if (system->GetTypeID() == systemTypeID) {
      return system;
    }
  }
  return nullptr;  // If no matching system found
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vAddSystem(std::shared_ptr<ECSystem> system) {
  std::unique_lock<std::mutex> lock(vecSystemsMutex);
  spdlog::debug("Adding system at address {}",
                static_cast<void*>(system.get()));
  m_vecSystems.push_back(std::move(system));
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vUpdate(float deltaTime) {
  // spdlog::debug("Update Filament API thread: 0x{:x}", pthread_self());

  // Copy systems under mutex and log details
  std::vector<std::shared_ptr<ECSystem>> systemsCopy;
  {
    std::unique_lock<std::mutex> lock(vecSystemsMutex);
    // spdlog::debug("m_vecSystems size: {}", m_vecSystems.size());

    for (size_t i = 0; i < m_vecSystems.size(); ++i) {
      // spdlog::debug("System {} at address {}", i,
      //               static_cast<void*>(m_vecSystems[i].get()));
      // m_vecSystems[i]->DebugPrint();
    }

    // Copy the systems vector
    systemsCopy = m_vecSystems;
  }  // Mutex is unlocked here

  // Iterate over the copy without holding the mutex
  for (const auto& system : systemsCopy) {
    if (system) {
      //spdlog::debug("Processing system at address {}, use_count={}",
      //              static_cast<void*>(system.get()), system.use_count());
      //spdlog::debug("Before DebugPrint");
      // system->DebugPrint();
      //spdlog::debug("After DebugPrint");

      // spdlog::debug("Before vProcessMessages");
      system->vProcessMessages();
      // spdlog::debug("After vProcessMessages");

      // spdlog::debug("vUpdate system at address {}",
      //               static_cast<void*>(system.get()));
      // spdlog::debug("Before vUpdate");
      system->vUpdate(deltaTime);
      // spdlog::debug("After vUpdate");
    } else {
      spdlog::error("Encountered null system pointer!");
    }
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::DebugPrint() {
  for (const auto& system : m_vecSystems) {
    spdlog::debug("ECSystemManager:: DebugPrintProcessing system at address {}, use_count={}",
                  static_cast<void*>(system.get()), system.use_count());
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vShutdownSystems() {
  asio::post(*ECSystemManager::GetInstance()->GetStrand(), [&] {
    for (const auto& system : m_vecSystems) {
      system->vShutdownSystem();
    }
  });
}

}  // namespace plugin_filament_view