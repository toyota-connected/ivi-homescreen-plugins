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

#include "backend_interface.h"

namespace video_player_linux {

// Baseline V4L2 M2M decoder backend. Priority 50 — above pure
// software (~0) and below every platform-specific backend (80..100),
// so it naturally takes the slot when no vendor backend matches.
// Built on GStreamer's v4l2codecs plugin (v4l2h264dec / v4l2slh264dec /
// v4l2h265dec / v4l2slh265dec / v4l2vp9dec / v4l2av1dec); probes for
// factory availability via GstElementFactory at construction time.
class GenericV4L2Backend final : public VideoDecoderBackend {
 public:
  std::string name() const override;
  int priority() const override;
  bool is_available() const override;
  BackendCapabilities query_capabilities() const override;
  GstElement* build_decoder_bin(const std::string& codec,
                                const DecoderConfig& cfg) override;
  GstElement* build_converter_bin(uint64_t src_modifier,
                                  uint64_t dst_modifier) override;
};

// Static initializers in a statically-linked library can get stripped
// when nothing references the translation unit. Call this function
// once from the plugin's init path (Phase 2.6) to guarantee the
// backend registers itself with BackendRegistry. No-op on repeat
// calls — BackendRegistry accepts the same backend name only once.
void RegisterGenericV4L2Backend();

}  // namespace video_player_linux
