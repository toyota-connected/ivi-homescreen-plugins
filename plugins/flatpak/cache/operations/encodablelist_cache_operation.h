/*
 * Copyright 2023-2025 Toyota Connected North America
 * Copyright 2025 Ahmed Wafdy
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

#ifndef ENCODABLELIST_CACHE_OPERATION_H
#define ENCODABLELIST_CACHE_OPERATION_H

#include <spdlog/spdlog.h>
#include <chrono>

#include <flutter/encodable_value.h>

#include "../../messages.g.h"
#include "../cache_manager.h"
#include "cache_operation_template.h"
#include "tomlplusplus/toml.hpp"

namespace flatpak_plugin {

struct EncodableListCacheOperation
    : public CacheOperationTemplate<flutter::EncodableList> {
 public:
  CacheManager* manager;
  explicit EncodableListCacheOperation(CacheManager* manager)
      : manager(manager) {}

  bool ValidateKey(const std::string& key) override { return !key.empty(); }

  std::string SerializeData(const flutter::EncodableList& data) override {
    try {
      const auto& codec = FlatpakApi::GetCodec();
      const flutter::EncodableValue encodable_data(data);
      const auto encoded = codec.EncodeMessage(encodable_data);
      if (!encoded) {
        spdlog::error("Failed to encode encodable list");
        return "";
      }
      return {encoded->begin(), encoded->end()};
    } catch (const std::exception& e) {
      spdlog::error("Failed to serialize encodable list: {}", e.what());
      return "";
    } catch (...) {
      spdlog::error("Failed to serialize encodable list");
      return "";
    }
  }

  std::optional<flutter::EncodableList> DeserializeData(
      const std::string& serialized_data) override {
    if (serialized_data.empty()) {
      return std::nullopt;
    }
    spdlog::debug("[EncodableListCache] Deserializing {} bytes of data",
                  serialized_data.size());
    try {
      if (serialized_data.empty()) {
        spdlog::error("Attempting to deserialize empty encodable list");
        return flutter::EncodableList{};
      }

      const auto& codec = FlatpakApi::GetCodec();
      std::vector<uint8_t> buffer(serialized_data.begin(),
                                  serialized_data.end());
      const auto decoded = codec.DecodeMessage(buffer);

      if (!decoded) {
        spdlog::error("Failed to decode message");
        return std::nullopt;
      }
      if (!std::holds_alternative<flutter::EncodableList>(*decoded)) {
        spdlog::error("Decoded message is not EncodableList");
        return std::nullopt;
      }

      const auto res = std::get<flutter::EncodableList>(*decoded);
      flutter::EncodableList encodable_data;
      encodable_data.reserve(res.size());
      for (const auto& item : res) {
        if (std::holds_alternative<flutter::CustomEncodableValue>(item)) {
          const auto& value = std::get<flutter::CustomEncodableValue>(item);
          if (value.type() == typeid(Application)) {
            try {
              const Application& app = std::any_cast<Application>(value);
              encodable_data.emplace_back(item);
            } catch (const std::exception& e) {
              spdlog::error(
                  "Failed to convert encodable list to Application: {}",
                  e.what());
              encodable_data.emplace_back(item);
            }
          } else {
            encodable_data.emplace_back(item);
          }
        } else {
          encodable_data.emplace_back(item);
        }
      }
      return encodable_data;
    } catch (const std::exception& e) {
      spdlog::error("Failed to deserialize message: {}", e.what());
      return std::nullopt;
    } catch (...) {
      spdlog::error("Failed to deserialize message");
      return std::nullopt;
    }
  }

  std::chrono::system_clock::time_point GetExpiryTime() override {
    return std::chrono::system_clock::now() + manager->config_.default_ttl;
  }

  bool ValidateData(const flutter::EncodableList& /* data */) override {
    // Accept empty lists as valid
    return true;
  }
};
}  // namespace flatpak_plugin

#endif  // ENCODABLELIST_CACHE_OPERATION_H
