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

// NXP i.MX 8M / 8M Plus backend built around the Hantro VPU exposed via
// NXP's gstreamer-imx port. Priority 100 so it wins over
// GenericV4L2Backend (priority 50) on i.MX boards that ship the NXP
// BSP. On mainline-kernel i.MX 8M Plus the same hardware is reachable
// through v4l2codecs (v4l2h264dec / v4l2h265dec) and
// GenericV4L2Backend will pick it up instead — that's intentional, no
// vendor-specific property set is required for the mainline path.
//
// Element name history (what the plan.md covers):
//   * Newer gstreamer-imx: imxvpudec_h264, imxvpudec_h265, imxvpudec_vp8,
//     imxvpudec_vp9
//   * Older: vpudec (generic "whatever the caps say")
//
// 8M Plus Hantro G2 H.265 emits NV12 in the DRM_FORMAT_MOD_ARM_NV12_4L4
// tiled modifier. We advertise it in query_capabilities() so callers
// that can sample tiled modifiers (Mali with the right extensions) see
// it, but we don't force it — the caller falls back through
// build_converter_bin() when its modifier set doesn't include the tile.
class Imx8mVpuBackend final : public VideoDecoderBackend {
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

// Explicit registration hook — see RegisterGenericV4L2Backend() for the
// rationale (static-lib initializer stripping). Idempotent.
void RegisterImx8mVpuBackend();

}  // namespace video_player_linux
