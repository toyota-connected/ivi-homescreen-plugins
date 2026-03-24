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

#ifndef PLUGINS_FLATPAK_CACHE_CURL_NETWORK_FETCHER_H
#define PLUGINS_FLATPAK_CACHE_CURL_NETWORK_FETCHER_H

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <encodable_value.h>

#include "../../flatpak_plugin.h"
#include "../interfaces/network_fetcher.h"
#include "common/curl_client/curl_client.h"

/**
 * @brief Curl-based network fetcher implementation
 *
 * Integrates with existing CurlClient to provide network operations
 * with proper error handling and retry logic.
 */
class CurlNetworkFetcher final : public INetworkFetcher {
 public:
  /**
   * @brief Constructs a CurlNetworkFetcher with specified timeout and retry
   * settings.
   *
   * @param timeout The timeout duration for network requests in seconds.
   * Defaults to 30 seconds.
   * @param max_retries The maximum number of retry attempts for failed
   * requests. Defaults to 3.
   */
  explicit CurlNetworkFetcher(
      std::chrono::seconds timeout = std::chrono::seconds(30),
      int max_retries = 3);

  ~CurlNetworkFetcher() override;

  // INetworkFetcher implementation
  std::optional<std::string> Fetch(
      const std::string& url,
      const std::vector<std::string>& headers) override;

  std::optional<std::string> Post(
      const std::string& url,
      const std::vector<std::pair<std::string, std::string>>& form_data,
      const std::vector<std::string>& headers) override;

  bool IsNetworkAvailable() override;

  long GetLastResponseCode() override;

  void SetBearerToken(const std::string& token) override;

  std::optional<flutter::EncodableList> FetchRemotes(
      const std::string& installation_id) override;

 private:
  std::unique_ptr<plugin_common_curl::CurlClient> curl_client_;
  std::atomic<long> last_response_code_{0};
  int max_retries_{3};

  /**
   * @brief Performs a network operation with automatic retry logic
   * @param operation The operation to be executed with retry logic. Should
   * return a string on success or indicate failure through the retry mechanism
   *
   * @return std::optional<std::string> The result of the operation if
   * successful, or std::nullopt if all retry attempts failed
   */
  template <typename Operation>
  std::optional<std::string> PerformWithRetry(Operation&& operation);

  void ProcessHeaders(const std::vector<std::string>& headers,
                      std::vector<std::string>& non_auth_headers) const;
};

#endif  // PLUGINS_FLATPAK_CACHE_CURL_NETWORK_FETCHER_H