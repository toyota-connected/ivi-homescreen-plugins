#pragma once
#include <functional>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>

#include "plugins/common/common.h"

#include "core/systems/messages/ecs_message.h"
#include "core/systems/messages/ecs_message_types.h"

namespace plugin_filament_view {

using ECSMessageHandler = std::function<void(const ECSMessage&)>;

class ECSystem {
 public:
  virtual ~ECSystem() = default;

  // Send a message to the system
  void vSendMessage(const ECSMessage& msg) {
    spdlog::debug("[vSendMessage] Attempting to acquire messagesMutex");
    std::unique_lock<std::mutex> lock(messagesMutex);
    spdlog::debug("[vSendMessage] messagesMutex acquired");
    messageQueue_.push(msg);
    spdlog::debug("[vSendMessage] Message pushed to queue. Queue size: {}",
                  messageQueue_.size());
  }

  // Register a message handler for a specific message type
  void vRegisterMessageHandler(ECSMessageType type,
                               const ECSMessageHandler& handler) {
    spdlog::debug(
        "[vRegisterMessageHandler] Attempting to acquire handlersMutex");
    std::unique_lock<std::mutex> lock(handlersMutex);
    spdlog::debug("[vRegisterMessageHandler] handlersMutex acquired");
    handlers_[type].push_back(handler);
    spdlog::debug(
        "[vRegisterMessageHandler] Handler registered for message type {}",
        static_cast<int>(type));
  }

  // Unregister all handlers for a specific message type
  void vUnregisterMessageHandler(ECSMessageType type) {
    spdlog::debug(
        "[vUnregisterMessageHandler] Attempting to acquire handlersMutex");
    std::unique_lock<std::mutex> lock(handlersMutex);
    spdlog::debug("[vUnregisterMessageHandler] handlersMutex acquired");
    handlers_.erase(type);
    spdlog::debug(
        "[vUnregisterMessageHandler] Handlers unregistered for message type {}",
        static_cast<int>(type));
  }

  // Clear all message handlers
  void vClearMessageHandlers() {
    spdlog::debug(
        "[vClearMessageHandlers] Attempting to acquire handlersMutex");
    std::unique_lock<std::mutex> lock(handlersMutex);
    spdlog::debug("[vClearMessageHandlers] handlersMutex acquired");
    handlers_.clear();
    spdlog::debug("[vClearMessageHandlers] All handlers cleared");
  }

  // Process incoming messages
  virtual void vProcessMessages() {
    //spdlog::debug("[vProcessMessages] Attempting to acquire messagesMutex");
    std::queue<ECSMessage> messagesToProcess;

    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      //spdlog::debug("[vProcessMessages] messagesMutex acquired");
      std::swap(messageQueue_, messagesToProcess);
      /*spdlog::debug(
          "[vProcessMessages] Swapped message queues. Messages to process: {}",
          messagesToProcess.size());*/
    }  // messagesMutex is unlocked here

    while (!messagesToProcess.empty()) {
      const ECSMessage& msg = messagesToProcess.front();
      //spdlog::debug("[vProcessMessages] Processing message");
      vHandleMessage(msg);
      messagesToProcess.pop();
      /*spdlog::debug(
          "[vProcessMessages] Message processed. Remaining messages: {}",
          messagesToProcess.size());*/
    }

    //spdlog::debug("[vProcessMessages] done");
  }

  virtual void vInitSystem() = 0;

  virtual void vUpdate(float /*deltaTime*/) {
    spdlog::debug("[vUpdate] Starting update");
    vProcessMessages();
    spdlog::debug("[vUpdate] Update completed");
  }

  virtual void vShutdownSystem() = 0;

  [[nodiscard]] virtual size_t GetTypeID() const = 0;

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(ECSystem).hash_code();
  }

  virtual void DebugPrint() = 0;

 protected:
  // Handle a specific message type by invoking the registered handlers
  virtual void vHandleMessage(const ECSMessage& msg) {
    spdlog::debug("[vHandleMessage] Attempting to acquire handlersMutex");
    std::vector<ECSMessageHandler> handlersToInvoke;
    {
      std::unique_lock<std::mutex> lock(handlersMutex);
      spdlog::debug("[vHandleMessage] handlersMutex acquired");
      for (const auto& [type, handlerList] : handlers_) {
        if (msg.hasData(type)) {
          spdlog::debug("[vHandleMessage] Message has data for type {}",
                        static_cast<int>(type));
          handlersToInvoke.insert(handlersToInvoke.end(), handlerList.begin(),
                                  handlerList.end());
        }
      }
    }  // handlersMutex is unlocked here
    spdlog::debug("[vHandleMessage] Handlers to invoke: {}",
                  handlersToInvoke.size());

    for (const auto& handler : handlersToInvoke) {
      spdlog::debug("[vHandleMessage] Invoking handler");
      try {
        handler(msg);
      } catch (const std::exception& e) {
        spdlog::error("[vHandleMessage] Exception in handler: {}", e.what());
      }
    }
    spdlog::debug("[vHandleMessage] Handlers invocation completed");
  }

 private:
  std::queue<ECSMessage> messageQueue_;  // Queue of incoming messages
  std::unordered_map<ECSMessageType,
                     std::vector<ECSMessageHandler>,
                     EnumClassHash>
      handlers_;  // Registered handlers

  std::mutex messagesMutex;
  std::mutex handlersMutex;
};

}  // namespace plugin_filament_view
