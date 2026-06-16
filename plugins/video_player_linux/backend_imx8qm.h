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

// NXP i.MX 8QM / 8QXP backend built around the Amphion Malone video
// codec. Unlike the 8M family (Hantro via gstreamer-imx), NXP exposes
// Amphion through the standard v4l2 codec interface — so the decoder
// factories are the same `v4l2h264dec` / `v4l2slh264dec` that the
// generic backend would pick. We still register a platform-specific
// backend at priority 100 because the Amphion output path has a
// critical quirk: frames come out in DRM_FORMAT_MOD_AMPHION_TILED,
// which current GL drivers on i.MX 8QM cannot sample directly.
//
// See plan.md §3.2 — required pipeline is:
//   v4l2h264dec (Amphion) → NV12:AMPHION_TILED dmabuf
//     → imxvideoconvert_g2d → NV12:LINEAR dmabuf
//     → appsink
//
// build_converter_bin() returns a wrapped imxvideoconvert_g2d element
// for the AMPHION_TILED → LINEAR transition. Pipeline callers that
// use the backend's build_*_bin() path get the detile for free; the
// playbin auto-plug path gets a warning in ConfigureAutoPluggedDecoder
// because playbin won't insert G2D on its own (tracked as a Phase 3.x
// follow-up when VideoPlayer takes over construction explicitly).
class Imx8QmBackend final : public VideoDecoderBackend {
 public:
  [[nodiscard]] std::string name() const override;
  [[nodiscard]] int priority() const override;
  [[nodiscard]] bool is_available() const override;
  [[nodiscard]] BackendCapabilities query_capabilities() const override;
  GstElement* build_decoder_bin(const std::string& codec,
                                const DecoderConfig& cfg) override;
  GstElement* build_converter_bin(uint64_t src_modifier,
                                  uint64_t dst_modifier) override;
  void ConfigureAutoPluggedDecoder(GstElement* dec,
                                   const std::string& codec,
                                   const DecoderConfig& cfg) override;
};

// Explicit registration hook — see RegisterGenericV4L2Backend() for
// the static-lib-stripping rationale. Idempotent.
void RegisterImx8QmBackend();

}  // namespace video_player_linux
