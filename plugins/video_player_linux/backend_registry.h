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

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend_interface.h"

namespace video_player_linux {

// Process-wide registry of hardware decoder backends. Backends register
// themselves via a static initializer at load time:
//
//   namespace {
//     [[maybe_unused]] auto reg_foo = [] {
//       BackendRegistry::instance().RegisterBackend(
//           std::make_unique<FooBackend>());
//       return 0;
//     }();
//   }
//
// Registration order doesn't matter — selection uses priority() +
// per-codec capability, not insertion order.
//
// Thread safety: registration happens at static-init time
// (single-threaded), selection happens from the Dart platform thread
// during VideoPlayer construction. The mutex guards the members so
// concurrent VideoPlayer creation is safe.
class BackendRegistry {
 public:
  BackendRegistry(BackendRegistry const&) = delete;
  BackendRegistry& operator=(BackendRegistry const&) = delete;

  static BackendRegistry& Instance();

  // Add a backend. Called from the static initializer of each
  // backend's TU. If the backend's is_available() returns false at
  // registration time, it's still kept in the list so future
  // Select() calls don't need to re-probe — but it won't be chosen.
  void RegisterBackend(std::unique_ptr<VideoDecoderBackend> backend);

  // Pick the highest-priority available backend that supports the
  // given codec on the given platform profile. Returns nullptr when
  // nothing fits (caller should fall back to playbin auto-plug).
  //
  // `profile` is a hint: a backend that lists itself as
  // platform-specific should decline non-matching profiles via
  // query_capabilities() rather than is_available() so the registry
  // can see it but not pick it.
  VideoDecoderBackend* Select(const std::string& codec,
                              PlatformProfile profile);

  // Look up a backend by the name it returned from
  // VideoDecoderBackend::name(). Used by config-driven selection
  // where the user wrote an explicit `h264_backend = "foo"` in the
  // TOML. Returns nullptr when no backend with that name is
  // registered.
  VideoDecoderBackend* FindByName(const std::string& name);

  // Debug / logging accessor. Returns raw pointers into registry
  // storage — don't store them past the BackendRegistry's lifetime
  // (which is process lifetime, so in practice this is fine).
  std::vector<VideoDecoderBackend*> All() const;

 private:
  BackendRegistry() = default;

  // query_capabilities() may be expensive (open /dev/videoN, enum
  // formats). Cache the result per-backend. Keyed by backend pointer
  // (stable for the lifetime of the registry).
  const BackendCapabilities& GetCaps(VideoDecoderBackend* backend);

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<VideoDecoderBackend>> backends_;
  std::unordered_map<VideoDecoderBackend*, BackendCapabilities> caps_cache_;
};

}  // namespace video_player_linux
