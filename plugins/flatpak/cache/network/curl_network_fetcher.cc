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

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <thread>

#include "curl_network_fetcher.h"

#include "tools/logging.h"

CurlNetworkFetcher::CurlNetworkFetcher(std::chrono::seconds /* timeout */,
                                       const int max_retries)
    : max_retries_(max_retries) {
  curl_client_ = std::make_unique<plugin_common_curl::CurlClient>();
}

CurlNetworkFetcher::~CurlNetworkFetcher() = default;

template <typename Operation>
std::optional<std::string> CurlNetworkFetcher::PerformWithRetry(
    Operation&& operation) {
  int attempts = 0;

  while (attempts <= max_retries_) {
    try {
      if (auto result = operation(); result.has_value()) {
        return result;
      }

      const long response_code = last_response_code_.load();
      if (response_code >= 200 && response_code < 300) {
        // success
        return std::nullopt;
      }

      if (response_code >= 500 || response_code == 408 ||
          response_code == 429) {
        ++attempts;
        if (attempts <= max_retries_) {
          // exponential backoff
          std::this_thread::sleep_for(
              std::chrono::seconds(1 << (attempts - 1)));
          continue;
        }
        return std::nullopt;
      }
    } catch (std::exception& e) {
      spdlog::error("Network operation failed: {}", e.what());
      ++attempts;
      spdlog::error("Retrying network operation (attempt {})", attempts);
      if (attempts <= max_retries_) {
        // exponential backoff
        std::this_thread::sleep_for(std::chrono::seconds(1 << (attempts - 1)));
        continue;
      }
      break;
    }
  }
  return std::nullopt;
}

std::optional<std::string> CurlNetworkFetcher::Fetch(
    const std::string& url,
    const std::vector<std::string>& headers) {
  auto operation = [this, &url, &headers]() -> std::optional<std::string> {
    try {
      std::vector<std::string> processed_headers;
      ProcessHeaders(headers, processed_headers);

      auto response = curl_client_->Get(url);
      last_response_code_.store(curl_client_->GetHttpCode());

      if (const long response_code = last_response_code_.load();
          response_code >= 200 && response_code < 300) {
        // success
        return response;
      }

      return std::nullopt;
    } catch (const std::exception& e) {
      spdlog::error("Network operation failed: {}", e.what());
      return std::nullopt;
    }
  };

  return PerformWithRetry(std::move(operation));
}

std::optional<std::string> CurlNetworkFetcher::Post(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& form_data,
    const std::vector<std::string>& headers) {
  auto operation = [this, &url, &form_data,
                    &headers]() -> std::optional<std::string> {
    try {
      std::vector<std::string> processed_headers;
      ProcessHeaders(headers, processed_headers);

      auto response = curl_client_->Post(url, form_data, headers);
      last_response_code_.store(curl_client_->GetHttpCode());

      if (const long response_code = last_response_code_.load();
          response_code >= 200 && response_code < 300) {
        // success
        return response;
      }

      return std::nullopt;
    } catch (const std::exception& e) {
      spdlog::error("Network operation failed: {}", e.what());
      return std::nullopt;
    }
  };

  return PerformWithRetry(std::move(operation));
}

bool CurlNetworkFetcher::IsNetworkAvailable() {
  try {
    auto result = curl_client_->Get("https://www.google.com");
    const long response_code = curl_client_->GetHttpCode();
    last_response_code_.store(response_code);

    return response_code > 0;
  } catch (const std::exception& e) {
    spdlog::error("Network operation failed: {}", e.what());
    return false;
  }
}

long CurlNetworkFetcher::GetLastResponseCode() {
  return last_response_code_.load();
}

void CurlNetworkFetcher::SetBearerToken(const std::string& token) {
  curl_client_->SetBearerToken(token);
}

std::optional<flutter::EncodableList> CurlNetworkFetcher::FetchRemotes(
    const std::string& installation_id) {
  try {
    // Get installation
    const auto installation_result =
        flatpak_plugin::FlatpakShim::get_remotes_by_installation_id(
            installation_id);
    if (installation_result.has_error()) {
      spdlog::error("[Network Fetcher] Error fetching remotes : {}",
                    installation_result.error().message());
      return std::nullopt;
    }

    return installation_result.value();
  } catch (const std::exception& e) {
    spdlog::error("Network operation failed: {}", e.what());
    return std::nullopt;
  }
}

void CurlNetworkFetcher::ProcessHeaders(
    const std::vector<std::string>& headers,
    std::vector<std::string>& non_auth_headers) const {
  non_auth_headers.clear();

  for (const auto& header : headers) {
    if (header.find("Authorization: Bearer ") == 0) {
      std::string token = header.substr(21);
      curl_client_->SetBearerToken(token);
    } else {
      non_auth_headers.push_back(header);
    }
  }
}