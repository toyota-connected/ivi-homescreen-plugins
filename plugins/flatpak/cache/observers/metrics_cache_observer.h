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

#ifndef PLUGINS_FLATPAK_CACHE_METRICS_CACHE_OBSERVER_H
#define PLUGINS_FLATPAK_CACHE_METRICS_CACHE_OBSERVER_H

#include "flatpak/cache/cache_config.h"
#include "flatpak/cache/interfaces/cache_observer.h"

#include "tools/logging.h"

class MetricsCacheObserver final : public ICacheObserver {
 public:
  explicit MetricsCacheObserver(flatpak_plugin::CacheMetrics* metrics);

  void OnCacheHit(const std::string& key, size_t data_size) override;

  void OnCacheMiss(const std::string& key) override;

  void OnCacheExpired(const std::string& key) override;

  void OnNetworkFallback(const std::string& reason) override;

  void OnNetworkError(const std::string& url, long error_code) override;

  void OnCacheCleanup(size_t entries_cleaned) override;

 private:
  flatpak_plugin::CacheMetrics* metrics_;
};

#endif  // PLUGINS_FLATPAK_CACHE_METRICS_CACHE_OBSERVER_H