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

// NXP i.MX 95 backend. Priority 105 — above the 8M (Hantro) and 8QM
// (Amphion) backends at 100, so it wins when both the i.MX 95
// platform AND the newer vendor elements are present.
//
// i.MX 95 is a substantial step up from the 8 series:
//   * Mali GPU (not Vivante) — cleaner mainline driver story
//   * VPU decodes H.264, H.265, VP9, and (new) AV1
//   * HDR10 / HLG metadata is preserved through the decode pipeline
//
// Element situation at the time of writing:
//   * AV1:    imxvpudec_av1 (NXP BSP only — no mainline v4l2 equivalent
//             yet)
//   * H.264 / H.265 / VP9: either NXP's imxvpudec_<codec> or the
//             mainline v4l2h264dec / v4l2h265dec / v4l2vp9dec set
//             (fall back in that order)
//
// Hardware is detected by DT compatible "fsl,imx95" OR by presence of
// imxvpudec_av1 (no i.MX 8 variant has AV1, so this is a reliable
// secondary signal when DT is incomplete or hidden behind ACPI on
// early silicon).
class Imx95Backend final : public VideoDecoderBackend {
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

// Explicit registration hook. See RegisterGenericV4L2Backend() for
// the static-lib-stripping rationale. Idempotent.
void RegisterImx95Backend();

}  // namespace video_player_linux
