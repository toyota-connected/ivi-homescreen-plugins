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

#ifndef PLUGINS_FLATPAK_CACHE_CACHE_OPERATION_TEMPLATE_H
#define PLUGINS_FLATPAK_CACHE_CACHE_OPERATION_TEMPLATE_H

#include <chrono>
#include <exception>
#include <optional>
#include <string>

#include "tools/logging.h"

#include "../interfaces/cache_storage.h"

/**
 * @brief Template class for cache operations providing a framework for data
 * caching and retrieval.
 *
 * This abstract template class defines a common interface for caching
 * operations with type safety. It provides methods for storing and retrieving
 * data from cache storage with validation, serialization, and error handling
 * capabilities.
 */
template <typename T>
class CacheOperationTemplate {
 protected:
  /**
   * @brief Validates if the provided key is acceptable for cache operations.
   * @param key The string key to validate
   * @return true if the key is valid and can be used for cache operations,
   *         false otherwise
   */
  virtual bool ValidateKey(const std::string& key) = 0;

  /**
   * @brief Serializes the given data object into a string representation.
   * @param data The data object of type T to be serialized
   * @return std::string The serialized representation of the data object
   */
  virtual std::string SerializeData(const T& data) = 0;

  /**
   * @brief Pure virtual method to deserialize cached data into type T
   * @return std::optional<T> Returns the deserialized object of type T if
   *         successful, or std::nullopt if deserialization fails or no
   *         valid data is available
   */
  virtual std::optional<T> DeserializeData(
      const std::string& serialized_data) = 0;

  /**
   * @brief Gets the expiry time for the cache operation.
   * @return std::chrono::system_clock::time_point The expiry time of the cache
   * operation
   */
  virtual std::chrono::system_clock::time_point GetExpiryTime() = 0;

  /**
   * @brief Validates the provided data object.
   * @param data The data object to be validated
   * @return true if the data is valid and passes all validation checks
   * @return false if the data is invalid or fails any validation checks
   */
  virtual bool ValidateData(const T& data) = 0;

 public:
  virtual ~CacheOperationTemplate() = default;

  /**
   * @brief Caches data with the specified key using the provided storage
   * interface
   * @tparam T The type of data to be cached (must be serializable)
   * @param key The unique identifier for the cached data (must pass validation)
   * @param data The data object to be stored in the cache
   * @param storage Pointer to the cache storage interface for persistence
   * operations
   *
   * @return true if the data was successfully cached, false if validation
   * failed or an exception occurred during the caching process
   */
  bool CacheData(const std::string& key,
                 const T& data,
                 ICacheStorage* storage) {
    if (!ValidateKey(key) || !ValidateData(data) || !storage) {
      return false;
    }

    try {
      auto serialized = SerializeData(data);
      auto expiry = GetExpiryTime();
      return storage->Store(key, serialized, expiry);
    } catch (const std::exception& e) {
      spdlog::error("[CacheOperation] Failed to cache data: {}", e.what());
      return false;
    }
  }

  /**
   * @brief Retrieves and deserializes data from cache storage using the
   * specified key.
   * @tparam T The type of data to retrieve and deserialize
   * @param key The cache key used to identify the stored data
   * @param storage Pointer to the cache storage interface for data retrieval
   * @return std::optional<T> The deserialized data if successful, std::nullopt
   * otherwise
   */
  std::optional<T> RetrieveData(const std::string& key,
                                ICacheStorage* storage) {
    if (!ValidateKey(key) || !storage) {
      return std::nullopt;
    }

    try {
      const auto serialized = storage->Retrieve(key);
      if (!serialized.has_value()) {
        return std::nullopt;
      }

      return DeserializeData(serialized.value());
    } catch (const std::exception& e) {
      spdlog::error("[CacheOperation] Failed to retrieve data: {}", e.what());
      return std::nullopt;
    }
  }
};

#endif  // PLUGINS_FLATPAK_CACHE_CACHE_OPERATION_TEMPLATE_H