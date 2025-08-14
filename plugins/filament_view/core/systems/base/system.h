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

#include <encodable_value.h>
#include <event_channel.h>
#include <functional>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include <core/lifecycle_participant.h>
#include <core/systems/messages/ecs_message.h>
#include <core/systems/messages/ecs_message_types.h>
#include <core/utils/filament_types.h>
#include <core/utils/identifiable_type.h>
#include <core/utils/smarter_pointers.h>

namespace flutter {
class PluginRegistrar;
class EncodableValue;
}  // namespace flutter

namespace plugin_filament_view {

class ECSManager;
class EntityObject;
class Component;
enum class ECSOperation;

using ECSMessageHandler = std::function<void(const ECSMessage&)>;

class System : public IdentifiableType, public LifecycleParticipant<ECSManager> {
  public:
    virtual ~System() = default;

    // Send a message to the system
    void SendMessage(const ECSMessage& msg);

    // Register a message handler for a specific message type
    void registerMessageHandler(ECSMessageType type, const ECSMessageHandler& handler);

    // Unregister all handlers for a specific message type
    void UnregisterMessageHandler(ECSMessageType type);

    // Clear all message handlers
    void ClearMessageHandlers();

    // Process incoming messages
    virtual void ProcessMessages();

    /// @brief Initialize the system with the ECSManager, then calls onSystemInit()
    void onInitialize(const ECSManager& params) override {
      ecs = const_cast<ECSManager*>(&params);
      onSystemInit();
    }

    /// @brief Called after the system is initialized, to perform any additional setup.
    /// Must be implemented by derived classes.
    virtual void onSystemInit() = 0;

    virtual void update(double /*deltaTime*/) override = 0;

    virtual void onDestroy() override = 0;

    virtual void debugPrint() = 0;

    void setupMessageChannels(
      flutter::PluginRegistrar* poPluginRegistrar,
      const std::string& szChannelName
    );

    void SendDataToEventChannel(const flutter::EncodableMap& oDataMap) const;

    /// Called by ECS when a new component is added
    virtual void onComponentOperation(
      EntityObject& entity,
      Component& component,
      ECSOperation operation
    );

  protected:
    smarter_raw_ptr<ECSManager> ecs = nullptr;

    // Handle a specific message type by invoking the registered handlers
    virtual void handleMessage(const ECSMessage& msg);

  private:
    std::queue<ECSMessage> messageQueue_;  // Queue of incoming messages
    std::unordered_map<
      ECSMessageType,
      std::vector<ECSMessageHandler>,
      EnumClassHash>
      handlers_;  // Registered handlers

    std::mutex messagesMutex;
    std::mutex handlersMutex;

    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> event_channel_;

    // The internal Flutter event sink instance, used to send events to the Dart
    // side.
    std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;
};

}  // namespace plugin_filament_view
