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

// Rockchip has two coexisting GStreamer integrations — automotive
// BSPs typically ship the vendor MPP stack, community distributions
// increasingly use mainline V4L2. Both backends are defined here; the
// registry sorts them by priority so MPP wins when present.
//
// MPP backend (priority 100)
// --------------------------
// Built on gst-rockchip (JeffyCN / BoxCloud forks). Elements:
//   mppvideodec (newer, unified)
//   mpph264dec  (older per-codec)
//   mpph265dec
//   mppvp9dec
//   mppav1dec   (RK3588 only)
// Exposes ARM AFBC as output modifier — the RK3588's killer feature
// for 4K60 bandwidth.
//
// V4L2 backend (priority 90 — below MPP, above GenericV4L2 at 50)
// ---------------------------------------------------------------
// Upstream gst-plugins-bad v4l2codecs. Element names match
// GenericV4L2's primary/fallback table; the subclass just raises the
// priority and gates on Rockchip platform-profile so we don't
// accidentally elevate the generic backend on non-Rockchip hardware.
class RockchipMppBackend final : public VideoDecoderBackend {
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

class RockchipV4L2Backend final : public VideoDecoderBackend {
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

// Registers both Rockchip backends. Idempotent.
void RegisterRockchipBackends();

}  // namespace video_player_linux
