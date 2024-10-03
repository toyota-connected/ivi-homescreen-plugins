#pragma once
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>
#include <iostream>

#include "core/systems/messages/ecs_message.h"
#include "core/systems/messages/ecs_message_types.h"

namespace plugin_filament_view {

using ECSMessageHandler = std::function<void(const ECSMessage&)>;

class ECSystem {
public:
    virtual ~ECSystem() = default;

    // Send a message to the system
    void vSendMessage(const ECSMessage& msg) {
        messageQueue_.push(msg);
    }

    // Register a message handler for a specific message type
    void vRegisterMessageHandler(ECSMessageType type, const ECSMessageHandler& handler) {
        handlers_[type].push_back(handler);
    }

    // Unregister all handlers for a specific message type
    void vUnregisterMessageHandler(ECSMessageType type) {
        handlers_.erase(type);
    }

    // Clear all message handlers
    void vClearMessageHandlers() {
        handlers_.clear();
    }

    // Process incoming messages
    virtual void vProcessMessages() {
        while (!messageQueue_.empty()) {
            const ECSMessage& msg = messageQueue_.front();
            vHandleMessage(msg);
            messageQueue_.pop();
        }
    }

    virtual void vInitSystem() = 0;
    virtual void vUpdate(float /*deltaTime*/) {
      vProcessMessages();
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
        for (const auto& [type, handlerList] : handlers_) {
            if (msg.hasData(type)) {
                for (const auto& handler : handlerList) {
                    handler(msg);
                }
            }
        }
    }

private:
    std::queue<ECSMessage> messageQueue_;  // Queue of incoming messages
    std::unordered_map<ECSMessageType, std::vector<ECSMessageHandler>, EnumClassHash> handlers_;  // Registered handlers
};
}