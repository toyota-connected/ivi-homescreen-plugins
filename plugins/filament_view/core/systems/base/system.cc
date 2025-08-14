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
#include "system.h"

#include <event_stream_handler_functions.h>
#include <functional>
#include <plugin_registrar.h>
#include <queue>
#include <standard_method_codec.h>
#include <unordered_map>
#include <vector>

#include <core/systems/messages/ecs_message.h>
#include <core/systems/messages/ecs_message_types.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
// Send a message to the system
void System::SendMessage(const ECSMessage& msg) {
  SPDLOG_TRACE("[SendMessage] Attempting to acquire messagesMutex");
  std::unique_lock lock(messagesMutex);
  SPDLOG_TRACE("[SendMessage] messagesMutex acquired");
  messageQueue_.push(msg);
  SPDLOG_TRACE("[SendMessage] Message pushed to queue. Queue size: {}", messageQueue_.size());
}

////////////////////////////////////////////////////////////////////////////
// Register a message handler for a specific message type
void System::registerMessageHandler(ECSMessageType type, const ECSMessageHandler& handler) {
  SPDLOG_TRACE("[registerMessageHandler] Attempting to acquire handlersMutex");
  std::unique_lock lock(handlersMutex);
  SPDLOG_TRACE("[registerMessageHandler] handlersMutex acquired");
  handlers_[type].push_back(handler);
  SPDLOG_TRACE(
    "[registerMessageHandler] Handler registered for message type {}", static_cast<int>(type)
  );
}

////////////////////////////////////////////////////////////////////////////
// Unregister all handlers for a specific message type
void System::UnregisterMessageHandler(ECSMessageType type) {
  SPDLOG_TRACE("[UnregisterMessageHandler] Attempting to acquire handlersMutex");
  std::unique_lock lock(handlersMutex);
  SPDLOG_TRACE("[UnregisterMessageHandler] handlersMutex acquired");
  handlers_.erase(type);
  SPDLOG_TRACE(
    "[UnregisterMessageHandler] Handlers unregistered for message type {}", static_cast<int>(type)
  );
}

////////////////////////////////////////////////////////////////////////////
// Clear all message handlers
void System::ClearMessageHandlers() {
  SPDLOG_TRACE("[ClearMessageHandlers] Attempting to acquire handlersMutex");
  std::unique_lock lock(handlersMutex);
  SPDLOG_TRACE("[ClearMessageHandlers] handlersMutex acquired");
  handlers_.clear();
  SPDLOG_TRACE("[ClearMessageHandlers] All handlers cleared");
}

////////////////////////////////////////////////////////////////////////////
// Process incoming messages
void System::ProcessMessages() {
  SPDLOG_TRACE("[ProcessMessages] Attempting to acquire messagesMutex");
  std::queue<ECSMessage> messagesToProcess;

  {
    std::unique_lock lock(messagesMutex);
    SPDLOG_TRACE("[ProcessMessages] messagesMutex acquired");
    std::swap(messageQueue_, messagesToProcess);
    SPDLOG_TRACE(
      "[ProcessMessages] Swapped message queues. Messages to process: {}", messagesToProcess.size()
    );
  }  // messagesMutex is unlocked here

  while (!messagesToProcess.empty()) {
    const ECSMessage& msg = messagesToProcess.front();
    SPDLOG_TRACE("[ProcessMessages] Processing message");
    handleMessage(msg);
    messagesToProcess.pop();
    SPDLOG_TRACE(
      "[ProcessMessages] Message processed. Remaining messages: {}", messagesToProcess.size()
    );
  }

  SPDLOG_TRACE("[ProcessMessages] done");
}

////////////////////////////////////////////////////////////////////////////
// Handle a specific message type by invoking the registered handlers
void System::handleMessage(const ECSMessage& msg) {
  SPDLOG_TRACE("[handleMessage] Attempting to acquire handlersMutex");
  std::vector<ECSMessageHandler> handlersToInvoke;
  {
    std::unique_lock lock(handlersMutex);
    SPDLOG_TRACE("[handleMessage] handlersMutex acquired");
    for (const auto& [type, handlerList] : handlers_) {
      if (msg.hasData(type)) {
        SPDLOG_TRACE("[handleMessage] Message has data for type {}", static_cast<int>(type));
        handlersToInvoke.insert(handlersToInvoke.end(), handlerList.begin(), handlerList.end());
      }
    }
  }  // handlersMutex is unlocked here
  SPDLOG_TRACE("[handleMessage] Handlers to invoke: {}", handlersToInvoke.size());

  for (const auto& handler : handlersToInvoke) {
    SPDLOG_TRACE("[handleMessage] Invoking handler");
    try {
      handler(msg);
    } catch (const std::exception& e) {
      spdlog::error("[handleMessage] Exception in handler: {}", e.what());
    }
  }
  SPDLOG_TRACE("[handleMessage] Handlers invocation completed");
}

////////////////////////////////////////////////////////////////////////////////////
void System::SendDataToEventChannel(const flutter::EncodableMap& oDataMap) const {
  if (!event_sink_ || !event_channel_) {
    return;
  }

  event_sink_->Success(flutter::EncodableValue(oDataMap));
}

void System::onComponentOperation(
  EntityObject& /*entity*/,
  Component& /*component*/,
  ECSOperation /*operation*/
) {}

////////////////////////////////////////////////////////////////////////////////////
void System::setupMessageChannels(
  flutter::PluginRegistrar* poPluginRegistrar,
  const std::string& szChannelName
) {
  if (event_channel_ != nullptr) {
    return;
  }

  SPDLOG_DEBUG("Creating Event Channel {}::{}", __FUNCTION__, szChannelName);

  event_channel_ = std::make_unique<flutter::EventChannel<>>(
    poPluginRegistrar->messenger(), szChannelName, &flutter::StandardMethodCodec::GetInstance()
  );

  event_channel_->SetStreamHandler(std::make_unique<flutter::StreamHandlerFunctions<>>(
    // This lambda is called when the stream is first subscribed to
    [&](
      const flutter::EncodableValue* /* arguments */, std::unique_ptr<flutter::EventSink<>>&& events
    ) -> std::unique_ptr<flutter::StreamHandlerError<>> {
      event_sink_ = std::move(events);
      return nullptr;
    },
    // This lambda is called when the stream is cancelled
    [&](const flutter::EncodableValue* /* arguments */)
      -> std::unique_ptr<flutter::StreamHandlerError<>> {
      event_sink_ = nullptr;
      return nullptr;
    }
  ));

  SPDLOG_DEBUG("Event Channel creation Complete for {}", szChannelName);
}

}  // namespace plugin_filament_view
