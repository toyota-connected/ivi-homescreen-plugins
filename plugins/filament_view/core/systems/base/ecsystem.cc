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
#include "ecsystem.h"

#include <functional>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>

#include "plugins/common/common.h"

#include "core/systems/messages/ecs_message.h"
#include "core/systems/messages/ecs_message_types.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
// Send a message to the system
void ECSystem::vSendMessage(const ECSMessage& msg) {
  SPDLOG_TRACE("[vSendMessage] Attempting to acquire messagesMutex");
  std::unique_lock<std::mutex> lock(messagesMutex);
  SPDLOG_TRACE("[vSendMessage] messagesMutex acquired");
  messageQueue_.push(msg);
  SPDLOG_TRACE("[vSendMessage] Message pushed to queue. Queue size: {}",
               messageQueue_.size());
}

////////////////////////////////////////////////////////////////////////////
// Register a message handler for a specific message type
void ECSystem::vRegisterMessageHandler(ECSMessageType type,
                                       const ECSMessageHandler& handler) {
  SPDLOG_TRACE("[vRegisterMessageHandler] Attempting to acquire handlersMutex");
  std::unique_lock<std::mutex> lock(handlersMutex);
  SPDLOG_TRACE("[vRegisterMessageHandler] handlersMutex acquired");
  handlers_[type].push_back(handler);
  SPDLOG_TRACE(
      "[vRegisterMessageHandler] Handler registered for message type {}",
      static_cast<int>(type));
}

////////////////////////////////////////////////////////////////////////////
// Unregister all handlers for a specific message type
void ECSystem::vUnregisterMessageHandler(ECSMessageType type) {
  SPDLOG_TRACE(
      "[vUnregisterMessageHandler] Attempting to acquire handlersMutex");
  std::unique_lock<std::mutex> lock(handlersMutex);
  SPDLOG_TRACE("[vUnregisterMessageHandler] handlersMutex acquired");
  handlers_.erase(type);
  SPDLOG_TRACE(
      "[vUnregisterMessageHandler] Handlers unregistered for message type {}",
      static_cast<int>(type));
}

////////////////////////////////////////////////////////////////////////////
// Clear all message handlers
void ECSystem::vClearMessageHandlers() {
  SPDLOG_TRACE("[vClearMessageHandlers] Attempting to acquire handlersMutex");
  std::unique_lock<std::mutex> lock(handlersMutex);
  SPDLOG_TRACE("[vClearMessageHandlers] handlersMutex acquired");
  handlers_.clear();
  SPDLOG_TRACE("[vClearMessageHandlers] All handlers cleared");
}

////////////////////////////////////////////////////////////////////////////
// Process incoming messages
void ECSystem::vProcessMessages() {
  SPDLOG_TRACE("[vProcessMessages] Attempting to acquire messagesMutex");
  std::queue<ECSMessage> messagesToProcess;

  {
    std::unique_lock<std::mutex> lock(messagesMutex);
    SPDLOG_TRACE("[vProcessMessages] messagesMutex acquired");
    std::swap(messageQueue_, messagesToProcess);
    SPDLOG_TRACE(
        "[vProcessMessages] Swapped message queues. Messages to process: {}",
        messagesToProcess.size());
  }  // messagesMutex is unlocked here

  while (!messagesToProcess.empty()) {
    const ECSMessage& msg = messagesToProcess.front();
    SPDLOG_TRACE("[vProcessMessages] Processing message");
    vHandleMessage(msg);
    messagesToProcess.pop();
    SPDLOG_TRACE("[vProcessMessages] Message processed. Remaining messages: {}",
                 messagesToProcess.size());
  }

  SPDLOG_TRACE("[vProcessMessages] done");
}

////////////////////////////////////////////////////////////////////////////
// Handle a specific message type by invoking the registered handlers
void ECSystem::vHandleMessage(const ECSMessage& msg) {
  SPDLOG_TRACE("[vHandleMessage] Attempting to acquire handlersMutex");
  std::vector<ECSMessageHandler> handlersToInvoke;
  {
    std::unique_lock<std::mutex> lock(handlersMutex);
    SPDLOG_TRACE("[vHandleMessage] handlersMutex acquired");
    for (const auto& [type, handlerList] : handlers_) {
      if (msg.hasData(type)) {
        SPDLOG_TRACE("[vHandleMessage] Message has data for type {}",
                     static_cast<int>(type));
        handlersToInvoke.insert(handlersToInvoke.end(), handlerList.begin(),
                                handlerList.end());
      }
    }
  }  // handlersMutex is unlocked here
  SPDLOG_TRACE("[vHandleMessage] Handlers to invoke: {}",
               handlersToInvoke.size());

  for (const auto& handler : handlersToInvoke) {
    SPDLOG_TRACE("[vHandleMessage] Invoking handler");
    try {
      handler(msg);
    } catch (const std::exception& e) {
      spdlog::error("[vHandleMessage] Exception in handler: {}", e.what());
    }
  }
  SPDLOG_TRACE("[vHandleMessage] Handlers invocation completed");
}

}  // namespace plugin_filament_view
