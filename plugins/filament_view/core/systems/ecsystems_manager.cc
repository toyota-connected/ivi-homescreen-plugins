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
    m_poInstance = new ECSystemManager();
  }

  return m_poInstance;
}

////////////////////////////////////////////////////////////////////////////
ECSystemManager::~ECSystemManager() {
  spdlog::debug("ECSystemManager~");
}

////////////////////////////////////////////////////////////////////////////
ECSystemManager::ECSystemManager()
    : io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(asio::make_work_guard(io_context_->get_executor())),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)),
      m_eCurrentState(RunState::NotInitialized) {
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

  m_eCurrentState = RunState::Running;
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
  m_eCurrentState = RunState::ShutdownStarted;

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
  // Note this is currently expected to be called from within
  // an already asio post, Leaving this commented out so you know
  // that you could change up the routine, but if you do
  // it need sto ru non the main thread.
  // asio::post(*ECSystemManager::GetInstance()->GetStrand(), [&] {
  for (const auto& system : m_vecSystems) {
    system->vInitSystem();
  }

  m_eCurrentState = RunState::Initialized;

  //});
}

////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ECSystem> ECSystemManager::poGetSystem(
    size_t systemTypeID,
    const std::string& where) {
  auto callingThread = pthread_self();
  if (callingThread != filament_api_thread_id_) {
    // Note we should have a 'log once' base functionality in common
    // creating this inline for now.
    auto foundIter = m_mapOffThreadCallers.find(where);
    if (foundIter == m_mapOffThreadCallers.end()) {
      spdlog::info(
          "From {} "
          "You're calling to get a system from an off thread, undefined "
          "experience!"
          " Use a message to do your work or grab the ecsystemmanager strand "
          "and "
          "do your work.",
          where);

      m_mapOffThreadCallers.insert(std::pair(where, 0));
    }
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
  // Copy systems under mutex
  std::vector<std::shared_ptr<ECSystem>> systemsCopy;
  {
    std::unique_lock<std::mutex> lock(vecSystemsMutex);

    // Copy the systems vector
    systemsCopy = m_vecSystems;
  }  // Mutex is unlocked here

  // Iterate over the copy without holding the mutex
  for (const auto& system : systemsCopy) {
    if (system) {
      system->vProcessMessages();
      system->vUpdate(deltaTime);
    } else {
      spdlog::error("Encountered null system pointer!");
    }
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::DebugPrint() {
  for (const auto& system : m_vecSystems) {
    spdlog::debug(
        "ECSystemManager:: DebugPrintProcessing system at address {}, "
        "use_count={}",
        static_cast<void*>(system.get()), system.use_count());
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSystemManager::vShutdownSystems() {
  asio::post(*ECSystemManager::GetInstance()->GetStrand(), [&] {
    for (const auto& system : m_vecSystems) {
      system->vShutdownSystem();
    }

    m_eCurrentState = RunState::Shutdown;
    ;
  });
}

}  // namespace plugin_filament_view