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

#include "backend_registry.h"

#include <algorithm>
#include <limits>

#include <plugins/common/common.h>

namespace video_player_linux {

BackendRegistry& BackendRegistry::Instance() {
  // Meyers singleton — guaranteed thread-safe init since C++11, and
  // destroyed in reverse construction order at process exit. That's
  // after static destructors of individual TUs, so backends calling
  // Instance() from their own cleanup (they shouldn't, but in case)
  // still see a live registry.
  static BackendRegistry instance;
  return instance;
}

void BackendRegistry::RegisterBackend(
    std::unique_ptr<VideoDecoderBackend> backend) {
  if (!backend) {
    return;
  }
  const std::string name = backend->name();
  const int prio = backend->priority();
  const bool avail = backend->is_available();
  std::lock_guard<std::mutex> lock(mutex_);
  SPDLOG_DEBUG("[VideoPlayer] Registering backend '{}' (prio={} available={})",
               name, prio, avail);
  backends_.push_back(std::move(backend));
}

VideoDecoderBackend* BackendRegistry::Select(const std::string& codec,
                                             PlatformProfile profile) {
  (void)profile;  // profile is a hint; backends encode platform fit via
                  // is_available() + query_capabilities().

  std::lock_guard<std::mutex> lock(mutex_);
  VideoDecoderBackend* best = nullptr;
  int best_prio = std::numeric_limits<int>::min();

  for (auto& b : backends_) {
    if (!b->is_available()) {
      continue;
    }
    // query_capabilities() is potentially expensive (open device,
    // enum formats), so cache inside the lock. This keeps the
    // registry simple — the cache lifetime matches the registry's,
    // i.e. process lifetime, which is fine for the small number
    // of backends we expect (< 10 typical).
    const auto& caps = GetCaps(b.get());
    const bool supports =
        std::any_of(caps.supported_codecs.begin(), caps.supported_codecs.end(),
                    [&](const std::string& c) { return c == codec; });
    if (!supports) {
      continue;
    }
    const int prio = b->priority();
    if (prio > best_prio) {
      best = b.get();
      best_prio = prio;
    }
  }

  if (best) {
    SPDLOG_DEBUG("[VideoPlayer] Backend '{}' selected for codec '{}' (prio={})",
                 best->name(), codec, best_prio);
  } else {
    SPDLOG_DEBUG("[VideoPlayer] No backend available for codec '{}'", codec);
  }
  return best;
}

VideoDecoderBackend* BackendRegistry::FindByName(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& b : backends_) {
    if (b->name() == name) {
      return b.get();
    }
  }
  return nullptr;
}

std::vector<VideoDecoderBackend*> BackendRegistry::All() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<VideoDecoderBackend*> out;
  out.reserve(backends_.size());
  for (const auto& b : backends_) {
    out.push_back(b.get());
  }
  return out;
}

const BackendCapabilities& BackendRegistry::GetCaps(
    VideoDecoderBackend* backend) {
  // Called under mutex_ by Select(). std::unordered_map insert is
  // safe under the caller's lock.
  auto it = caps_cache_.find(backend);
  if (it == caps_cache_.end()) {
    it = caps_cache_.emplace(backend, backend->query_capabilities()).first;
  }
  return it->second;
}

}  // namespace video_player_linux
