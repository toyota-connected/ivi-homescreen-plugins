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

#ifndef PLUGINS_FLATPAK_CACHE_INSTALLATION_CACHE_OPERATION_H
#define PLUGINS_FLATPAK_CACHE_INSTALLATION_CACHE_OPERATION_H

#include <chrono>

#include "../../messages.g.h"
#include "../cache_manager.h"
#include "cache_operation_template.h"
#include "encodablelist_cache_operation.h"

namespace flatpak_plugin {
/**
 * @brief Handles caching operations for Flatpak installations with a specified
 * time-to-live (TTL).
 *
 * This class inherits from CacheOperationTemplate specialized for the
 * Installation type. It provides mechanisms to validate cache keys, serialize
 * and deserialize Installation data, determine expiry times, and validate
 * cached data.
 */
struct InstallationCacheOperation final : CacheOperationTemplate<Installation> {
  CacheManager* manager;
  std::unique_ptr<EncodableListCacheOperation> operation;

  explicit InstallationCacheOperation(CacheManager* manager)
      : manager(manager),
        operation(std::make_unique<EncodableListCacheOperation>(manager)) {}

 protected:
  bool ValidateKey(const std::string& key) override { return !key.empty(); }

  std::string SerializeData(const Installation& data) override {
    if (!operation) {
      spdlog::error("EncodableListCacheOperation is not initialized");
      return "";
    }
    // Serialize to List then Serialize the list
    const flutter::EncodableList list = data.ToEncodableList();
    return operation->SerializeData(list);
  }

  std::optional<Installation> DeserializeData(
      const std::string& serialized_data) override {
    if (serialized_data.empty()) {
      return std::nullopt;
    }

    const auto deserialization = operation->DeserializeData(serialized_data);

    if (!deserialization) {
      return std::nullopt;
    }
    const flutter::EncodableList list = *deserialization;
    if (list.empty()) {
      return Installation("", "", "", false, false, 0, flutter::EncodableList{},
                          flutter::EncodableList{}, flutter::EncodableList{});
    }

    return Installation::FromEncodableList(list);
  }

  std::chrono::system_clock::time_point GetExpiryTime() override {
    return std::chrono::system_clock::now() + manager->config_.default_ttl;
  }

  bool ValidateData(const Installation& data) override {
    return !data.id().empty() && !data.display_name().empty();
  }
};
}  // namespace flatpak_plugin

#endif  // PLUGINS_FLATPAK_CACHE_INSTALLATION_CACHE_OPERATION_H