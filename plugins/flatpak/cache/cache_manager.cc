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
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <flutter/encodable_value.h>

#include "cache_config.h"
#include "cache_manager.h"
#include "encodablelist_cache_operation.h"
#include "flatpak_installation_cache_operation.h"
#include "interfaces/cache_observer.h"
#include "interfaces/cache_storage.h"
#include "network/curl_network_fetcher.h"
#include "../flatpak_shim.h"
#include "storage/sqlite_cache_storage.h"

namespace flatpak_plugin {

CacheManager::CacheManager(CacheConfig config)
    : config_(std::move(config)), metrics_{} {}

CacheManager::CacheManager(CacheConfig config,
                           std::unique_ptr<ICacheStorage> storage,
                           std::unique_ptr<INetworkFetcher> fetcher)
    : storage_(std::move(storage)),
      network_fetcher_(std::move(fetcher)),
      config_(std::move(config)),
      metrics_{} {
  if (Initialize()) {
    spdlog::info("Cache manager initialized");
  }
}

CacheManager::~CacheManager() noexcept {
  stop_cleanup_.store(true);
  cleanup_cv_.notify_all();
  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }
}

bool CacheManager::Initialize() {
  {
    std::lock_guard lock(init_mutex_);
    spdlog::info(
        "Initializing CacheManager with config: db_path={}, ttl={}, policy={}, "
        "max_size={}",
        config_.db_path, config_.default_ttl.count(),
        static_cast<int>(config_.policy), config_.max_cache_size_mb);

    if (!storage_) {
      storage_ = std::make_unique<SQLiteCacheStorage>(
          config_.db_path, config_.enable_compression);
    }
    if (!storage_->Initialize()) {
      spdlog::error("Failed to initialize cache storage");
      return false;
    }

    if (!network_fetcher_) {
      network_fetcher_ = std::make_unique<CurlNetworkFetcher>(
          config_.network_timeout, config_.max_retries);
    }

    if (config_.enable_metrics) {
      metrics_.hits = 0;
      metrics_.misses = 0;
      metrics_.cache_size_bytes = 0;
      metrics_.network_calls = 0;
      metrics_.network_errors = 0;
      metrics_.start_time = std::chrono::system_clock::now();
    }

    is_initialized_ = true;
    spdlog::info("Cache Manager initialized successfully");
  }

  if (config_.enable_auto_cleanup) {
    cleanup_thread_ = std::thread(&CacheManager::CleanupWorker, this);
  }

  return true;
}

std::optional<flutter::EncodableList> CacheManager::GetApplicationsInstalled(
    const bool force_refresh) {
  if (!is_initialized_) {
    spdlog::error("Cache manager is not initialized");
    return std::nullopt;
  }

  const std::string key = GenerateKey("applications_installed");
  if (force_refresh) {
    InvalidateKey(key);
  }

  EncodableListCacheOperation cache_operation(this);

  auto network_ops = [this]() -> std::optional<flutter::EncodableList> {
    std::lock_guard lock(flatpak_mutex_);
    try {
      const auto plugin_ = std::make_unique<FlatpakShim>();
      const auto apps_result = FlatpakShim::GetApplicationsInstalled();

      if (apps_result.has_error()) {
        spdlog::error(
            "[FlatpakPlugin] Failed to get applications installed: {}",
            apps_result.error().message());
        return std::nullopt;
      }

      const flutter::EncodableList& apps = apps_result.value();
      if (apps.empty()) {
        spdlog::info(
            "[FlatpakPlugin] GetApplicationInstalled returned empty list");
        return flutter::EncodableList{};
      }
      return apps;
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Exception in GetApplicationsInstalled: {}",
                    e.what());
      return std::nullopt;
    } catch (...) {
      spdlog::error(
          "[FlatpakPlugin] Unknown error in GetApplicationsInstalled");
      return std::nullopt;
    }
  };

  try {
    spdlog::debug("Performing cache operation with key: {}", key);
    auto result = PerformCacheOperation<flutter::EncodableList>(
        key, network_ops, &cache_operation);
    if (result.has_value()) {
      spdlog::debug("Cache operation completed successfully, returned {} items",
                    result->size());
    } else {
      spdlog::debug("Cache operation returned no data");
    }
    return result;
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Error during cache operation: {}", e.what());
    return std::nullopt;
  } catch (...) {
    spdlog::error("[FlatpakPlugin] PerformCacheOperation: unknown error");
    return std::nullopt;
  }
}

std::optional<flutter::EncodableList> CacheManager::GetApplicationsRemote(
    const std::string& remote_id,
    const bool force_refresh) {
  if (!is_initialized_) {
    spdlog::error("Cache manager is not initialized");
    return std::nullopt;
  }

  const std::string key = GenerateKey("applications_remote", {remote_id});
  if (force_refresh) {
    InvalidateKey(key);
  }

  EncodableListCacheOperation cache_operation(this);

  auto network_ops = [this,
                      remote_id]() -> std::optional<flutter::EncodableList> {
    std::lock_guard lock(flatpak_mutex_);
    try {
      const auto plugin_ = std::make_unique<FlatpakShim>();
      const auto apps_result = FlatpakShim::GetApplicationsRemote(remote_id);

      if (apps_result.has_error()) {
        spdlog::error(
            "[FlatpakPlugin] Failed to get applications from remote {}: {}",
            remote_id, apps_result.error().message());
        return std::nullopt;
      }

      const flutter::EncodableList& apps = apps_result.value();
      if (apps.empty()) {
        spdlog::error(
            "[FlatpakPlugin] GetApplicationsRemote returned empty list for "
            "remote: {}",
            remote_id);
        return flutter::EncodableList{};
      }
      return apps;
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Exception in GetApplicationsRemote: {}",
                    e.what());
      return std::nullopt;
    } catch (...) {
      spdlog::error("[FlatpakPlugin] Unknown error in GetApplicationsRemote");
      return std::nullopt;
    }
  };

  try {
    spdlog::debug("Performing cache operation with key: {}", key);
    auto result = PerformCacheOperation<flutter::EncodableList>(
        key, network_ops, &cache_operation);
    if (result.has_value()) {
      spdlog::debug(
          "Cache operation completed successfully, returned {} items from "
          "remote {}",
          result->size(), remote_id);
    } else {
      spdlog::debug("Cache operation returned no data for remote {}",
                    remote_id);
    }
    return result;
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Error during cache operation: {}", e.what());
    return std::nullopt;
  } catch (...) {
    spdlog::error("[FlatpakPlugin] PerformCacheOperation: unknown error");
    return std::nullopt;
  }
}

std::optional<flutter::EncodableList> CacheManager::GetSystemInstallations(
    const bool force_refresh) {
  if (!is_initialized_) {
    spdlog::error("Cache manager is not initialized");
    return std::nullopt;
  }

  const std::string key = GenerateKey("system_installations");
  if (force_refresh) {
    InvalidateKey(key);
  }

  EncodableListCacheOperation cache_operation(this);

  auto network_ops = [this]() -> std::optional<flutter::EncodableList> {
    std::lock_guard lock(flatpak_mutex_);
    try {
      const auto plugin_ = std::make_unique<FlatpakShim>();
      const auto system_installations = FlatpakShim::GetSystemInstallations();

      if (system_installations.has_error()) {
        spdlog::error("[FlatpakPlugin] Failed to GetSystemInstallations: {}",
                      system_installations.error().message());
        return std::nullopt;
      }

      flutter::EncodableList installations = system_installations.value();
      if (installations.empty()) {
        spdlog::error(
            "[FlatpakPlugin] GetSystemInstallations returned empty list");
        return flutter::EncodableList{};
      }

      return installations;
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Exception in GetSystemInstallations: {}",
                    e.what());
      return std::nullopt;
    } catch (...) {
      spdlog::error("[FlatpakPlugin] Unknown error in GetSystemInstallations");
      return std::nullopt;
    }
  };

  try {
    spdlog::debug("Performing cache operation with key: {}", key);
    auto result = PerformCacheOperation<flutter::EncodableList>(
        key, network_ops, &cache_operation);
    if (result.has_value()) {
      spdlog::debug("Cache operation completed successfully, returned {} items",
                    result->size());
    } else {
      spdlog::debug("Cache operation returned no data");
    }
    return result;
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Error during cache operation: {}", e.what());
    return std::nullopt;
  } catch (...) {
    spdlog::error("[FlatpakPlugin] PerformCacheOperation: unknown error");
    return std::nullopt;
  }
}

std::optional<flutter::EncodableList> CacheManager::GetRemotes(
    const std::string& installation_id,
    const bool force_refresh) {
  if (!is_initialized_) {
    spdlog::error("Cache manager is not initialized");
    return std::nullopt;
  }

  const std::string key = GenerateKey("remotes", {installation_id});
  if (force_refresh) {
    InvalidateKey(key);
  }

  EncodableListCacheOperation cache_operation(this);

  auto network_ops =
      [this, installation_id]() -> std::optional<flutter::EncodableList> {
    std::lock_guard lock(flatpak_mutex_);
    try {
      return network_fetcher_->FetchRemotes(installation_id);
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] FetchRemotes failed: {}", e.what());
      return std::nullopt;
    }
  };

  try {
    spdlog::debug("Performing cache operation with key: {}", key);
    auto result = PerformCacheOperation<flutter::EncodableList>(
        key, network_ops, &cache_operation);
    if (result.has_value()) {
      spdlog::debug(
          "Cache operation completed successfully, returned {} remotes",
          result->size());
    } else {
      spdlog::debug("Cache operation returned no data");
    }
    return result;
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Error during cache operation: {}", e.what());
    return std::nullopt;
  } catch (...) {
    spdlog::error("[FlatpakPlugin] Unknown error in PerformCacheOperation");
    return std::nullopt;
  }
}

std::optional<Installation> CacheManager::GetUserInstallation(
    const bool force_refresh) {
  if (!is_initialized_) {
    spdlog::error("Cache manager is not initialized");
    return std::nullopt;
  }

  const std::string key = GenerateKey("user_installation");
  if (force_refresh) {
    InvalidateKey(key);
  }

  InstallationCacheOperation cache_operation(this);

  auto network_ops = [this]() -> std::optional<Installation> {
    std::lock_guard lock(flatpak_mutex_);
    try {
      const auto plugin_ = std::make_unique<FlatpakShim>();
      const auto installations_result = FlatpakShim::GetUserInstallation();

      if (installations_result.has_error()) {
        spdlog::error("[FlatpakPlugin] Failed to get user installations: {}",
                      installations_result.error().message());
        return std::nullopt;
      }

      const auto& installation = installations_result.value();
      spdlog::debug("[FlatpakPlugin] Got user installation: {}",
                    installation.id());
      return installation;
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Exception in GetUserInstallation: {}",
                    e.what());
      return std::nullopt;
    } catch (...) {
      spdlog::error("[FlatpakPlugin] Unknown error in GetUserInstallation");
      return std::nullopt;
    }
  };

  try {
    spdlog::debug("Performing cache operation with key: {}", key);
    auto result =
        PerformCacheOperation<Installation>(key, network_ops, &cache_operation);
    if (result.has_value()) {
      spdlog::debug(
          "Cache operation completed successfully, returned Installation ID {}",
          result->id());
    } else {
      spdlog::debug("Cache operation returned no data");
    }
    return result;
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Error during cache operation: {}", e.what());
    return std::nullopt;
  } catch (...) {
    spdlog::error("[FlatpakPlugin] PerformCacheOperation: unknown error");
    return std::nullopt;
  }
}

void CacheManager::AddObserver(std::unique_ptr<ICacheObserver> observer) const {
  std::lock_guard lock(observers_mutex_);
  observers_.push_back(std::move(observer));
}

void CacheManager::SetBearerToken(const std::string& token) const {
  std::lock_guard lock(network_mutex_);
  if (network_fetcher_) {
    network_fetcher_->SetBearerToken(token);
  }
}

std::string CacheManager::GenerateKey(const std::string& base_key,
                                      const std::vector<std::string>& params) {
  std::ostringstream oss;
  oss << base_key;
  for (const auto& param : params) {
    oss << ":" << param;
  }
  return oss.str();
}

void CacheManager::NotifyObservers(
    const std::function<void(ICacheObserver*)>& notification) const {
  std::vector<ICacheObserver*> observers_copy;

  {
    std::lock_guard lock(observers_mutex_);
    observers_copy.reserve(observers_.size());
    for (const auto& observer : observers_) {
      if (observer) {
        observers_copy.push_back(observer.get());
      }
    }
  }

  for (auto* observer : observers_copy) {
    try {
      notification(observer);
    } catch (const std::exception& e) {
      spdlog::error("Observer notification failed: {}", e.what());
    } catch (...) {
      spdlog::error("Observer notification failed with unknown exception");
    }
  }
}

void CacheManager::CleanupWorker() {
  std::unique_lock lock(cleanup_mutex_);
  while (!stop_cleanup_.load()) {
    cleanup_cv_.wait_for(lock, config_.cleanup_interval,
                         [this] { return stop_cleanup_.load(); });

    if (stop_cleanup_.load())
      break;

    try {
      size_t cleaned = 0;
      {
        std::lock_guard cache_lock(storage_mutex_);
        if (storage_) {
          cleaned = storage_->CleanupExpired();
        }
      }

      if (cleaned > 0) {
        spdlog::info("Cleaned up {} expired cache entries", cleaned);
        NotifyObservers([cleaned](ICacheObserver* observer) {
          observer->OnCacheCleanup(cleaned);
        });
      }
    } catch (const std::exception& e) {
      spdlog::error("Error during cache cleanup: {}", e.what());
    } catch (...) {
      spdlog::error("Unknown error during cache cleanup");
    }
  }
  spdlog::info("Cleanup thread finished");
}

template <typename T>
std::optional<T> CacheManager::PerformCacheOperation(
    const std::string& key,
    std::function<std::optional<T>()> network_operation,
    CacheOperationTemplate<T>* cache_operation) {
  CachePolicy current_policy;
  {
    std::lock_guard lock(config_mutex_);
    current_policy = config_.policy;
  }

  std::optional<T> result;

  switch (current_policy) {
    case CachePolicy::CACHE_ONLY: {
      result = GetFromCache<T>(key, cache_operation);
      if (result.has_value()) {
        IncrementMetric(MetricType::HIT);
        NotifyObservers(
            [key](ICacheObserver* observer) { observer->OnCacheHit(key, 0); });
      } else {
        IncrementMetric(MetricType::MISS);
        NotifyObservers(
            [key](ICacheObserver* observer) { observer->OnCacheMiss(key); });
      }
      break;
    }

    case CachePolicy::NETWORK_ONLY: {
      result = TryNetworkOperation(key, network_operation);
      break;
    }

    case CachePolicy::CACHE_FIRST: {
      result = GetFromCache<T>(key, cache_operation);
      if (result.has_value()) {
        IncrementMetric(MetricType::HIT);
        NotifyObservers(
            [key](ICacheObserver* observer) { observer->OnCacheHit(key, 0); });
      } else {
        IncrementMetric(MetricType::MISS);
        NotifyObservers(
            [key](ICacheObserver* observer) { observer->OnCacheMiss(key); });
        result = TryNetworkAndCache(key, network_operation, cache_operation);
      }
      break;
    }

    case CachePolicy::NETWORK_FIRST: {
      result = TryNetworkOperation(key, network_operation);
      if (!result.has_value()) {
        result = cache_operation->RetrieveData(key, storage_.get());
        if (result.has_value()) {
          NotifyObservers([key](ICacheObserver* observer) {
            observer->OnCacheHit(key, 0);
          });
        }
      }
      break;
    }
  }

  return result;
}

template <typename T>
std::optional<T> CacheManager::GetFromCache(
    const std::string& key,
    CacheOperationTemplate<T>* cache_operation) {
  std::lock_guard lock(storage_mutex_);
  if (!storage_) {
    return std::nullopt;
  }
  return cache_operation->RetrieveData(key, storage_.get());
}

template <typename T>
bool CacheManager::StoreInCache(const std::string& key,
                                const T& data,
                                CacheOperationTemplate<T>* cache_operation) {
  std::lock_guard lock(storage_mutex_);
  if (!storage_) {
    return false;
  }
  return cache_operation->CacheData(key, data, storage_.get());
}

template <typename T>
std::optional<T> CacheManager::TryNetworkOperation(
    const std::string& key,
    std::function<std::optional<T>()> network_operation) {
  if (!network_operation) {
    return std::nullopt;
  }

  try {
    if (config_.enable_metrics) {
      ++metrics_.network_calls;
    }
    auto result = network_operation();
    if (result.has_value()) {
      NotifyObservers([](ICacheObserver* observer) {
        observer->OnNetworkFallback("Data fetched from Network");
      });
    }
    return result;
  } catch (const std::exception& e) {
    if (config_.enable_metrics) {
      ++metrics_.network_errors;
      spdlog::error("Network operation failed for key {}: {}", key, e.what());
      NotifyObservers([&key](ICacheObserver* observer) {
        observer->OnNetworkError(key, -1);
      });
    }
    return std::nullopt;
  }
}

template <typename T>
std::optional<T> CacheManager::TryNetworkAndCache(
    const std::string& key,
    std::function<std::optional<T>()> network_operation,
    CacheOperationTemplate<T>* cache_operation) {
  auto result = TryNetworkOperation(key, std::move(network_operation));
  if (result.has_value() && storage_) {
    if (!cache_operation->CacheData(key, result.value(), storage_.get())) {
      spdlog::error("Failed to cache data for Key: {}", key);
    }
  }
  return result;
}

void CacheManager::IncrementMetric(MetricType type) const {
  std::lock_guard lock(metrics_mutex_);

  if (!config_.enable_metrics) {
    return;
  }

  switch (type) {
    case MetricType::HIT:
      ++metrics_.hits;
      break;
    case MetricType::MISS:
      ++metrics_.misses;
      break;
    case MetricType::NETWORK_CALL:
      ++metrics_.network_calls;
      break;
    case MetricType::NETWORK_ERROR:
      ++metrics_.network_errors;
      break;
  }
}

void CacheManager::InvalidateAll() const {
  {
    std::lock_guard lock(storage_mutex_);
    if (storage_) {
      storage_->Invalidate("");
    }
  }
  NotifyObservers(
      [](ICacheObserver*) { spdlog::info("All cache entries invalidated"); });
}

void CacheManager::InvalidateKey(const std::string& key) const {
  {
    std::lock_guard lock(storage_mutex_);
    if (storage_) {
      storage_->Invalidate(key);
    }
  }
  spdlog::info("Invalidated cache key: '{}'", key);
  NotifyObservers(
      [&key](ICacheObserver* observer) { observer->OnCacheExpired(key); });
}

bool CacheManager::IsHealthy() const {
  std::lock_guard lock(storage_mutex_);
  if (!storage_) {
    return false;
  }

  {
    std::lock_guard network_lock(network_mutex_);
    if (network_fetcher_ && !network_fetcher_->IsNetworkAvailable()) {
      spdlog::error("Network not available");
    }
  }

  size_t current_size = storage_->GetCacheSize();
  if (current_size > config_.max_cache_size_mb * 1024 * 1024) {
    spdlog::error("Cache size {} exceeds limit {}", current_size,
                  config_.max_cache_size_mb * 1024 * 1024);
  }

  spdlog::info("[FlatpakPlugin] Cache is healthy, cache size: {}",
               current_size);
  return true;
}

size_t CacheManager::GetCacheSize() const {
  std::lock_guard lock(storage_mutex_);
  return storage_ ? storage_->GetCacheSize() : 0;
}

void CacheManager::SetCachePolicy(CachePolicy policy) {
  std::lock_guard lock(config_mutex_);
  config_.policy = policy;
  spdlog::info("Cache policy changed to {}", static_cast<int>(policy));
}

CachePolicy CacheManager::GetCachePolicy() const {
  std::lock_guard lock(config_mutex_);
  return config_.policy;
}

size_t CacheManager::ForceCleanup() const {
  std::lock_guard lock(storage_mutex_);
  size_t cleaned = storage_->CleanupExpired();
  NotifyObservers([cleaned](ICacheObserver* observer) {
    observer->OnCacheCleanup(cleaned);
  });
  spdlog::info("Manual cleanup removed {} expired entries", cleaned);
  return cleaned;
}

const CacheConfig& CacheManager::GetConfig() const {
  return config_;
}

bool CacheManager::ExportCache(const std::string& filepath) const {
  std::lock_guard lock(storage_mutex_);
  try {
    if (const std::ofstream file(filepath, std::ios::binary); !file.is_open()) {
      spdlog::error("Failed to open export file: {}", filepath);
      return false;
    }
    spdlog::info("Cache exported to {}", filepath);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to export cache: {}", e.what());
    return false;
  }
}

bool CacheManager::ImportCache(const std::string& filepath) const {
  std::lock_guard lock(storage_mutex_);
  try {
    if (const std::ifstream file(filepath, std::ios::binary); !file.is_open()) {
      spdlog::error("Failed to open import file: {}", filepath);
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to import cache: {}", e.what());
    return false;
  }
}

std::unique_ptr<CacheManager> CacheManager::Builder::Build() {
  if (!storage_) {
    storage_ = std::make_unique<SQLiteCacheStorage>(config_.db_path,
                                                    config_.enable_compression);
  }
  if (!fetcher_) {
    fetcher_ = std::make_unique<CurlNetworkFetcher>(config_.network_timeout,
                                                    config_.max_retries);
  }

  return std::make_unique<CacheManager>(std::move(config_), std::move(storage_),
                                        std::move(fetcher_));
}

template std::optional<flutter::EncodableList>
CacheManager::PerformCacheOperation<flutter::EncodableList>(
    const std::string&,
    std::function<std::optional<flutter::EncodableList>()>,
    CacheOperationTemplate<flutter::EncodableList>*);

template std::optional<Installation> CacheManager::PerformCacheOperation<
    Installation>(const std::string&,
                  std::function<std::optional<Installation>()>,
                  CacheOperationTemplate<Installation>*);

template std::optional<flutter::EncodableList>
CacheManager::TryNetworkOperation<flutter::EncodableList>(
    const std::string&,
    std::function<std::optional<flutter::EncodableList>()>);

template std::optional<Installation> CacheManager::TryNetworkOperation<
    Installation>(const std::string&,
                  std::function<std::optional<Installation>()>);

template std::optional<flutter::EncodableList>
CacheManager::TryNetworkAndCache<flutter::EncodableList>(
    const std::string&,
    std::function<std::optional<flutter::EncodableList>()>,
    CacheOperationTemplate<flutter::EncodableList>*);

template std::optional<Installation> CacheManager::TryNetworkAndCache<
    Installation>(const std::string&,
                  std::function<std::optional<Installation>()>,
                  CacheOperationTemplate<Installation>*);

template std::optional<flutter::EncodableList> CacheManager::GetFromCache<
    flutter::EncodableList>(const std::string&,
                            CacheOperationTemplate<flutter::EncodableList>*);

template std::optional<Installation> CacheManager::GetFromCache<Installation>(
    const std::string&,
    CacheOperationTemplate<Installation>*);

template bool CacheManager::StoreInCache<flutter::EncodableList>(
    const std::string&,
    const flutter::EncodableList&,
    CacheOperationTemplate<flutter::EncodableList>*);

template bool CacheManager::StoreInCache<Installation>(
    const std::string&,
    const Installation&,
    CacheOperationTemplate<Installation>*);

}  // namespace flatpak_plugin