#pragma once
#include <any>
#include <unordered_map>
#include <iostream>
#include <stdexcept>
#include <typeinfo>

#include "ecs_message_types.h"

namespace plugin_filament_view {

struct EnumClassHash {
    template <typename T>
    std::size_t operator()(T t) const {
        return static_cast<std::size_t>(t);
    }
};

// Message class that can hold variable data amounts
class ECSMessage {
public:
    // Add data to the message
    template<typename T>
    void addData(ECSMessageType type, const T& value) {
        data_[type] = value;
    }

    // Get data from the message
    template<typename T>
    T getData(ECSMessageType type) const {
        auto it = data_.find(type);
        if (it != data_.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast& e) {
                throw std::runtime_error("Type mismatch for key. Expected type: " + std::string(typeid(T).name()));
            }
        }
        throw std::runtime_error("Message type not found");
    }

    // Check if the message contains a specific type
    bool hasData(ECSMessageType type) const {
        return data_.find(type) != data_.end();
    }

private:
    std::unordered_map<ECSMessageType, std::any, EnumClassHash> data_;
};
}
